/**
//
// BEGIN SONGBIRD GPL
// 
// This file is part of the Songbird web player.
//
// Copyright(c) 2005-2008 POTI, Inc.
// http://songbirdnest.com
// 
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the "GPL").
// 
// Software distributed under the License is distributed 
// on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either 
// express or implied. See the GPL for the specific language 
// governing rights and limitations.
//
// You should have received a copy of the GPL along with this 
// program. If not, go to http://www.gnu.org/licenses/gpl.html
// or write to the Free Software Foundation, Inc., 
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
// 
// END SONGBIRD GPL
//
 */
const Cc = Components.classes;
const Ci = Components.interfaces;
const Cr = Components.results;
const Ce = Components.Exception;
const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://app/jsmodules/SBJobUtils.jsm");
Cu.import("resource://app/jsmodules/ArrayConverter.jsm");
Cu.import("resource://app/jsmodules/sbProperties.jsm");
Cu.import("resource://app/jsmodules/sbLibraryUtils.jsm");
Cu.import("resource://app/jsmodules/StringUtils.jsm");

// used to identify directory import profiling runs
var gCounter = 0;

__defineGetter__("gIOService", function(){
  delete gIOService;
  gIOService = Cc["@mozilla.org/network/io-service;1"]
                 .getService(Ci.nsIIOService);
  return gIOService;
});

/******************************************************************************
 * Object implementing sbIDirectoryImportJob, responsible for finding media
 * items on disk, adding them to the library and performing a metadata scan.
 *
 * Call begin() to start the job.
 *****************************************************************************/
function DirectoryImportJob(aInputArray, 
                            aTargetMediaList, 
                            aTargetIndex, 
                            aImportService) {
  if (!(aInputArray instanceof Ci.nsIArray && 
        aInputArray.length > 0) ||
      !(aTargetMediaList instanceof Ci.sbIMediaList)) {
    throw Components.results.NS_ERROR_INVALID_ARG;
  }
  // Call super constructor
  SBJobUtils.JobBase.call(this);

  this._inputFiles = ArrayConverter.JSArray(aInputArray);
  this.targetMediaList = aTargetMediaList;
  this.targetIndex = aTargetIndex;

  this._importService = aImportService;
    
  // TODO these strings probably need updating
  this._titleText = SBString("media_scan.scanning");
  this._statusText =  SBString("media_scan.adding");
  
  // Initially cancelable
  this._canCancel = true;
  
  if ("@songbirdnest.com/Songbird/TimingService;1" in Cc) {
    this._timingService = Cc["@songbirdnest.com/Songbird/TimingService;1"]
                            .getService(Ci.sbITimingService);
    this._timingIdentifier = "DirImport" + gCounter++;
  }
}
DirectoryImportJob.prototype = {
  __proto__: SBJobUtils.JobBase.prototype,
  
  QueryInterface: XPCOMUtils.generateQI(
    [Ci.sbIDirectoryImportJob, Ci.sbIJobProgress, Ci.sbIJobProgressListener,
     Ci.sbIJobCancelable, Ci.nsIObserver, Ci.nsIClassInfo]),
  
  /** For nsIClassInfo **/
  getInterfaces: function(count) {
    var interfaces = [Ci.sbIDirectoryImportJob, Ci.sbIJobProgress, Ci.sbIJobProgressListener,
                      Ci.sbIJobCancelable, Ci.nsIClassInfo, Ci.nsISupports];
    count.value = interfaces.length;
    return interfaces;
  },

  totalAddedToMediaList     : 0,
  totalAddedToLibrary       : 0,
  totalDuplicates           : 0,  
  targetMediaList           : null,
  targetIndex               : null,
  
  // nsITimer used to poll the filescanner.  *sigh*
  _pollingTimer             : null,
  FILESCAN_POLL_INTERVAL    : 33,
  
  // sbIFileScan used to find media files in directories
  _fileScanner              : null,
  _fileScanQuery            : null,
  
  // Array of nsIFile directory paths or files
  _inputFiles               : null,
  _fileExtensions           : null,
  
  // The sbIDirectoryImportService.  Called back on job completion.
  _importService            : null,
  
  // JS Array of URI strings for all found media items
  _itemURIStrings           : null,
  
  // Rather than create all the items in one pass, then scan them all,
  // we want to create, read, repeat with small batches.  This avoids
  // wasting/fragmenting memory when importing 10,000+ tracks.
  // TODO tweak
  BATCHCREATE_SIZE          : 300,
  
  // Index into _itemURIStrings for the beginning of the
  // next create/read/add batch
  _nextURIIndex             : 0,
  
  // Temporary nsIArray of previously unknown sbIMediaItems, 
  // used to pass newly created items to a metadata scan job. 
  // Set in _onItemCreation.
  _currentMediaItems        : null,
    
  // True if we've forced the library into a batch state for
  // performance reasons
  _inLibraryBatch           : false,
  
  // Used to track performance
  _timingService            : null,
  _timingIdentifier         : null,
  
  /**
   * Get an enumerator over all media items found in this job.  
   * Will return an empty enumerator until the batch creation 
   * phase is complete.  Best called when the entire job is done.
   */
  enumerateAllItems: function DirectoryImportJob_enumerateAllItems() {
    // Ultimately the batch create job would give us this list, 
    // but at the moment we have to recreate it ourselves
    // using the list of URIs from the file scan.
    
    // If no URIs, just return the empty enumerator
    if (this._itemURIStrings.length == 0) {
      return ArrayConverter.enumerator([]);
    }
    
    // If all the URIs resulted in new items, and 
    // were processed in a single batch, then we can
    // just return the current items.
    if (this._currentMediaItems && 
        this._itemURIStrings.length == this._currentMediaItems.length) {
      return this._currentMediaItems.enumerate();
    }
    
    
    // Otherwise, we'll need to get media items for all the
    // URIs that have been added.  We want to avoid instantiating 
    // all the items at once (since there may be hundreds of thousands),
    // so instead get a few at a time. 

    var uriEnumerator = ArrayConverter.enumerator(this._itemURIStrings);
    var library = this.targetMediaList.library;
    const BATCHSIZE = this.BATCHCREATE_SIZE;

    // This enumerator instantiates the new media items on demand.
    var enumerator = {
      mediaItems: [],
      
      // sbIMediaListEnumerationListener, used to fetch items
      onEnumerationBegin: function() {},
      onEnumeratedItem: function(list, item) {
        this.mediaItems.push(item);
      },
      onEnumerationEnd: function() {},
      
      // nsISimpleEnumerator, used to dispense items
      hasMoreElements: function() { 
        return (this.mediaItems.length || 
                uriEnumerator.hasMoreElements());
      },
      getNext: function() {
        // When the buffer runs out, fetch more items from the library
        if (this.mediaItems.length == 0) {
          // Get the next set of URIs
          var propertyArray = SBProperties.createArray();
          var counter = 0;
          while (uriEnumerator.hasMoreElements() && counter < BATCHSIZE) {
            var itemURIStr = uriEnumerator.getNext().QueryInterface(Ci.nsISupportsString);
            propertyArray.appendProperty(SBProperties.contentURL, itemURIStr.data);
            counter++;
          } 
          library.enumerateItemsByProperties(propertyArray, this);
        }
        
        return this.mediaItems.shift();
      },
      
      QueryInterface: XPCOMUtils.generateQI([
          Ci.nsISimpleEnumerator, Ci.sbIMediaListEnumerationListener]),
    };
    
    return enumerator;
  },
    
  
  /**
   * Begin the import job
   */
  begin: function DirectoryImportJob_begin() {
    if (this._timingService) {
      this._timingService.startPerfTimer(this._timingIdentifier); 
    }
    
    // Start by finding all the files in the given directories
    
    var Application = Cc["@mozilla.org/fuel/application;1"]
                        .getService(Ci.fuelIApplication);
    
    this._fileScanner = Cc["@songbirdnest.com/Songbird/FileScan;1"]
                          .createInstance(Components.interfaces.sbIFileScan);

    // Figure out what files we are looking for.
    var typeSniffer = Cc["@songbirdnest.com/Songbird/Mediacore/TypeSniffer;1"]
                        .createInstance(Ci.sbIMediacoreTypeSniffer);
    try {
      var extensions = typeSniffer.mediaFileExtensions;
      if (!Application.prefs.getValue("songbird.mediascan.enableVideoImporting", true)) {
        // disable video, so scan only audio - see bug 13173
        extensions = typeSniffer.audioFileExtensions;
      }
      this._fileExtensions = [];
      while (extensions.hasMore()) {
        this._fileExtensions.push(extensions.getNext());
      }
    } catch (e) {
      dump("WARNING: DirectoryImportJob_begin could not find supported file extensions.  " +
           "Assuming test mode, and using a hardcoded list.\n");
      this._fileExtensions = ["mp3", "ogg", "flac"];
    }
    
    // XXX If possible, wrap the entire operation in an update batch
    // so that onbatchend listeners dont go to work between the 
    // end of the batchcreate and the start of the metadata scan.
    // This is an ugly hack, but it prevents a few second hang
    // when importing 100k tracks.
    var library = this.targetMediaList.library;
    if (library instanceof Ci.sbILocalDatabaseLibrary) {
      this._inLibraryBatch = true;
      library.forceBeginUpdateBatch();
    }
    
    this._startNextDirectoryScan();
    
    // Now poll the file scan, since it apparently perf is much better this way
    this._pollingTimer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    this._pollingTimer.init(this, this.FILESCAN_POLL_INTERVAL, Ci.nsITimer.TYPE_REPEATING_SLACK);
  },
  
  /**
   * Kick off a FileScanQuery for the next directory in _inputFiles.
   * Assumes there are directories left to scan.
   */
  _startNextDirectoryScan: function DirectoryImportJob__startNextDirectoryScan() {
    // Just report the error rather then throwing, since this method
    // is called by the timer.
    if (this._fileScanQuery && this._fileScanQuery.isScanning()) {
      Cu.reportError(
        "DirectoryImportJob__startDirectoryScan called with a scan in progress?");
      return;
    }
    
    var file = this._inputFiles.shift(); // Process directories in the order provided
    
    if (file && file instanceof Ci.nsIFileURL) {
      file = file.file;
    }
    
    // If something is messed up, just report and wait. The poll function will
    // move on to the next step.
    if (!file || !(file instanceof Ci.nsIFile)) {
      Cu.reportError("DirectoryImportJob__startNextDirectoryScan: invalid directory");
      return;
    }
    
    // We use the same _fileScanQuery object for scanning multiple directories.
    if (!this._fileScanQuery) {
      this._fileScanQuery = Cc["@songbirdnest.com/Songbird/FileScanQuery;1"]
                              .createInstance(Components.interfaces.sbIFileScanQuery);
      var fileScanQuery = this._fileScanQuery;
      this._fileExtensions.forEach(function(ext) { fileScanQuery.addFileExtension(ext) });
    }
    
    if (file.exists() && file.isDirectory()) {
      this._fileScanQuery.setDirectory(file.path);
      this._fileScanQuery.setRecurse(true);
      this._fileScanner.submitQuery(this._fileScanQuery);
    } else {
      if (!this._itemURIStrings) {
        this._itemURIStrings = [];
      }
      var urispec = gIOService.newFileURI(file).spec;
      var supportsString = Cc["@mozilla.org/supports-string;1"]
                             .createInstance(Ci.nsISupportsString);
      supportsString.data = urispec;
      this._itemURIStrings.push(supportsString);
    }
  },
  
  /** 
   * Called by _pollingTimer. Checks on the current fileScanQuery,
   * reporting progress and checking for completion.
   */
  _onPollFileScan: function DirectoryImportJob__onPollFileScan() {
    
    // If the current query is finished, collect the results and move on to
    // the next directory
    if (!this._fileScanQuery.isScanning()) {
      // If there are more directories to scan, start the next one
      if (this._inputFiles.length > 0) {
        this._startNextDirectoryScan();
      } else {
        // Otherwise, we're done scanning directories, and it is time to collect
        // information and create media items
        var fileCount = this._fileScanQuery.getFileCount();
        if (fileCount > 0 ) {
          var strings = this._fileScanQuery.getResultRangeAsURIStrings(0, fileCount - 1);
          if (!this._itemURIStrings) {
            this._itemURIStrings = ArrayConverter.JSArray(strings);
          } else {
            Array.prototype.push.apply(this._itemURIStrings, ArrayConverter.JSArray(strings));
          }
        }
        this._finishFileScan();
        this._startMediaItemCreation();
      }
      // If the file scan query is still running just update the UI
    } else {
      var text = this._fileScanQuery.getLastFileFound();
      text = text.split("/").pop();
      if (text.length > 60) {
        text = text.substring(0, 10) + "..." + text.substring(text.length - 40);
      }
      this._statusText = text;
      this.notifyJobProgressListeners();
    }
  },
  
  /** 
   * Called when the file scanner finishes with the target directories.
   * Responsible for shutting down the scanner.
   */
  _finishFileScan: function DirectoryImportJob__finishFileScan() {
    if (this._fileScanner) {
      this._fileScanner = null;
    }
    if (this._pollingTimer) {
      this._pollingTimer.cancel();
      this._pollingTimer = null;
    }
    // Set total number of items to process
    this._total = this._itemURIStrings.length;
  },
  
  /** 
   * Begin creating sbIMediaItems for a batch of found media URIs.
   * Does BATCHCREATE_SIZE at a time, looping back after 
   * onJobDelegateCompleted.
   */
  _startMediaItemCreation: 
  function DirectoryImportJob__startMediaItemCreation() {
    if (!this._itemURIStrings || !this._fileScanQuery) {
      Cu.reportError(
        "DirectoryImportJob__startMediaItemCreation called with invalid state");
      this.complete();
      return;
    }
    if (this._nextURIIndex >= this._itemURIStrings.length) {
      this.complete();
      return;
    }
    
    // Update status
    this._statusText = SBString("media_scan.adding");    
    this.notifyJobProgressListeners();
    
    // Bug 10228 - this needs to be replaced with an sbIJobProgress interface
    var thisJob = this;
    var batchCreateListener = {
      onProgress: function(aIndex) {},
      onComplete: function(aMediaItems, aResult) {
        thisJob._onItemCreation(aMediaItems, aResult);
      }
    };

    // Process the URIs a slice at a time, since creating all 
    // of them at once may require a very large amount of memory.
    var size = Math.min(this.BATCHCREATE_SIZE,
                        this._itemURIStrings.length - this._nextURIIndex);
    var endIndex = this._nextURIIndex + size;
    var uris = this._itemURIStrings.slice(this._nextURIIndex, endIndex);
    this._nextURIIndex = endIndex;
    
    // BatchCreateMediaItems needs to return an sbIJobProgress
    this.targetMediaList.library.batchCreateMediaItemsAsync(
        batchCreateListener, ArrayConverter.nsIArray(uris), null, false);
  },
  
  /** 
   * Called by sbILibrary.batchCreateMediaItemsAsync. 
   * BatchCreateMediaItemsAsync needs to be updated to actually send progress.
   * At the moment it only notifies when the process is over.
   */
  _onItemCreation: function DirectoryImportJob__onItemCreation(aMediaItems, aResult) {
    // Get the completed item array.  Don't use the given item array on error.
    // Use an empty one instead.
    if (Components.isSuccessCode(aResult)) {
      this._currentMediaItems = aMediaItems;
    } else {
      Cu.reportError("DirectoryImportJob__onItemCreation: aResult == " + aResult);
      this._currentMediaItems = Components.classes["@songbirdnest.com/moz/xpcom/threadsafe-array;1"]
                                      .createInstance(Components.interfaces.nsIArray);
    }
    
    if (this._currentMediaItems.length > 0) {
      this.totalAddedToLibrary += this._currentMediaItems.length;
      this.totalDuplicates += this._itemURIStrings.length - this._currentMediaItems.length;
      
      // Make sure we have metadata for all the added items
      this._startMetadataScan();
    } else {
      // no items were created, probably because they were all old.
      // try the next batch.
      this._startMediaItemCreation();
    }
  },
  
  /** 
   * Begin finding the metadata for the current set of media items.
   * Forward sbIJobProgress through to the metadata job
   */
  _startMetadataScan: 
  function DirectoryImportJob__startMetadataScan() {
    if (this._currentMediaItems && this._currentMediaItems.length > 0) {
      
      var metadataService = Cc["@songbirdnest.com/Songbird/FileMetadataService;1"]
                              .getService(Ci.sbIFileMetadataService);
      var metadataJob = metadataService.read(this._currentMediaItems);
        
      // Pump metadata job progress to the UI
      this.delegateJobProgress(metadataJob);
    } else {
      // Nothing to do. 
      this.complete();
    }
  },

  /** 
   * Insert found media items into the specified target location
   */
  _insertFoundItemsIntoTarget: 
  function DirectoryImportJob__insertFoundItemsIntoTarget() {
    // If we are inserting into a list, there is more to do than just importing 
    // the tracks into its library, we also need to insert all the items (even
    // the ones that previously existed) into the list at the requested position
    if (!(this.targetMediaList instanceof Ci.sbILibrary)) {
      try {
        if (this._itemURIStrings.length > 0) {
          var originalLength = this.targetMediaList.length;
          // If we need to insert, then do so
          if ((this.targetMediaList  instanceof Ci.sbIOrderableMediaList) && 
              (this.targetIndex >= 0) && 
              (this.targetIndex < this.targetMediaList.length)) {
            this.targetMediaList.insertSomeBefore(this.targetIndex, this.enumerateAllItems());
          } else {
            // Otherwise, just add
            this.targetMediaList.addSome(this.enumerateAllItems());
          }
          this.totalAddedToMediaList = this.targetMediaList.length - originalLength;
        }
      } catch (e) {
        Cu.reportError(e); 
      }
    }
  },

  /**
   * Called when a sub-job (set via Job.delegateJobProgress) completes.
   */
  onJobDelegateCompleted: function DirectoryImportJob_onJobDelegateCompleted() {

    // Track overall progress, so that the progress bar reflects
    // the total number of items to process, not the individual 
    // batches.
    this._progress += this._jobProgressDelegate.progress;
    
    // Stop delegating
    this.delegateJobProgress(null);
    
    // For now the only job we delegate to is metadata... so when 
    // it completes we go back to create the next set of media items.
    this._startMediaItemCreation();
  },

  /**
   * Override JOBBase.progress, to make sure the metadata scan progress
   * bar reflects the total item count, not the current batch
   */
  get progress() {
    return (this._jobProgressDelegate) ? 
      this._jobProgressDelegate.progress + this._progress : this._progress;
  },
  
  get total() {
    return this._total;
  },
  
  /**
   * Override JOBBase.titleText, since we want to maintain
   * the same title throughout the import process
   */
  get titleText() {
    return this._titleText;
  },

  /** sbIJobCancelable **/
  cancel: function DirectoryImportJob_cancel() {
    if (!this.canCancel) {
      throw new Error("DirectoryImportJob not currently cancelable")
    }
    
    if (this._fileScanner) {
      this._finishFileScan();
      this.complete();
    } else if (this._jobProgressDelegate) {
      // Cancelling the sub-job will trigger onJobDelegateCompleted
      this._jobProgressDelegate.cancel();
    }
    
    if (this._fileScanQuery) {
      this._fileScanQuery.cancel();
      this._fileScanQuery = null;   
    }
    
    // Remove anything that we've only partially processed
    if (this._currentMediaItems && this._currentMediaItems.length > 0) {
      this.targetMediaList.library.removeSome(this._currentMediaItems.enumerate());
      this.totalAddedToMediaList = 0;
      this.totalAddedToLibrary = 0;
      this.totalDuplicates = 0;
    }
  },
  
  /**
   * Stop everything, complete the job.
   */
  complete: function DirectoryImportJob_complete() {
    
    // Handle inserting into a media list at a specific index
    this._insertFoundItemsIntoTarget();
    
    this._status = Ci.sbIJobProgress.STATUS_SUCCEEDED;
    this._statusText = SBString("media_scan.complete");
    this.notifyJobProgressListeners();
    
    this._importService.onJobComplete();
    
    // XXX If we forced the library into a batch mode
    // in order to improve performance, make sure 
    // we end the batch (if we fail to do this
    // the tree view will never update)
    var library = this.targetMediaList.library;
    if (library instanceof Ci.sbILocalDatabaseLibrary &&
        this._inLibraryBatch) 
    {
      this._inLibraryBatch = false;
      library.forceEndUpdateBatch();
      
      // XXX Performance hack
      // If we've imported a ton of items then most of the 
      // library database is probably in memory.  
      // This isn't useful, and makes a bad first impression,
      // so lets just dump the entire DB cache.
      if (this.totalAddedToLibrary > this.BATCHCREATE_SIZE) {
        var dbEngine = Cc["@songbirdnest.com/Songbird/DatabaseEngine;1"]
                                     .getService(Ci.sbIDatabaseEngine);
        dbEngine.releaseMemory();
      }
    }
    
    if (this._fileScanQuery) {
      this._fileScanQuery.cancel();
      this._fileScanQuery = null;   
    }
    
    if (this._timingService) {
      this._timingService.stopPerfTimer(this._timingIdentifier);
    }
    
  },
  
  /**
   * nsIObserver, for nsITimer
   */
  observe: function DirectoryImportJob_observe(aSubject, aTopic, aData) {
    this._onPollFileScan();
  }
}






/******************************************************************************
 * Object implementing sbIDirectoryImportService. Used to start a 
 * new media import job.
 *****************************************************************************/
function DirectoryImportService() {  
  this._importJobs = [];
}

DirectoryImportService.prototype = {
  classDescription: "Songbird Directory Import Service",
  classID:          Components.ID("{6e542f90-44a0-11dd-ae16-0800200c9a66}"),
  contractID:       "@songbirdnest.com/Songbird/DirectoryImportService;1",
  QueryInterface:   XPCOMUtils.generateQI([Ci.sbIDirectoryImportService]),
  
  // List of pending jobs.  We only want to allow one to run at a time, 
  // since this can lock up the UI.
  _importJobs: null,
  
  /**
   * \brief Import any media files found in the given directories to 
   *        the specified media list.
   */
  import: function DirectoryImportService_import(aDirectoryArray, aTargetMediaList, aTargetIndex) {

    // Default to main library if not target is provided
    if (!aTargetMediaList) {
      aTargetMediaList = LibraryUtils.mainLibrary;
    }
    
    var job = new DirectoryImportJob(aDirectoryArray, aTargetMediaList, aTargetIndex, this);
    
    this._importJobs.push(job);
    
    // If this is the only job, just start immediately.
    // Otherwise it will be started when the current job finishes.
    if (this._importJobs.length == 1) {
      job.begin();
    }
    
    return job;
  },
  
  /**
   * \brief Called by the active import job when it completes
   */
  onJobComplete: function DirectoryImportService_onJobComplete() {
    this._importJobs.shift();
    if (this._importJobs.length > 0) {
      this._importJobs[0].begin();
    }
  }

} // DirectoryImportService.prototype


function NSGetModule(compMgr, fileSpec) {
  return XPCOMUtils.generateModule([DirectoryImportService]);
}
