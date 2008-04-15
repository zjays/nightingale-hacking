/*
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

/**
 * \brief Test creation and execution of metadata writing jobs
 */
 
var gTestFileLocation = "testharness/metadatamanager/files/";

// TODO Confirm this extension list. Don't forget to update files/Makefile.in as well.
var gSupportedFileExtensions = [
    "mp3", "flac", "mpc", "m4a", "mov", "m4p", 
    "m4v", "ogg", "oga", "ogv", "ogm", "ogx"
  ];
  

/**
 * Copy some media files into a temp directory, then confirm that metadata
 * properties can be modified using a write job.
 */
function runTest() {
  
  // Make a copy of everything in the test file folder
  // so that our changes don't interfere with other tests
  var testFolder = getCopyOfFolder(newAppRelativeFile(gTestFileLocation), "_temp_writing_files");

  // Now find all the media files in our testing directory
  var urls = getMediaFilesInFolder(testFolder);

  // Make sure we have files to test
  assertEqual(urls.length > 0, true);
  
  ///////////////////////////
  // Import the test items //
  ///////////////////////////
  var library = createNewLibrary( "test_metadatajob_writing_library" );
  var items = importFilesToLibrary(urls, library);
  assertEqual(items.length, urls.length);
  
  var job = startMetadataJob(items, Components.interfaces.sbIMetadataJob.JOBTYPE_READ);
  
  // Wait for reading to complete before continuing
  job.setObserver(new MetadataJobObserver(function onReadComplete(aSubject, aTopic, aData) {
    assertEqual(aTopic, "complete");
    assertTrue(aSubject.completed);
    job.removeObserver();
  
    //////////////////////////////////////
    // Save new metadata into the files //
    //////////////////////////////////////

    var unicodeSample =
            "\u008C\u00A2\u00FE" + // Latin-1 Sup
            "\u141A\u142B\u1443" + // Canadian Aboriginal
            "\u184F\u1889\u1896" + // Mongolian
            "\u4E08\u4E02\u9FBB" + // CJK Unified Extension
            "\u4DCA\u4DC5\u4DD9" + // Hexagram Symbols
            "\u0308\u030F\u034F" + // Combining Diacritics
            "\u033C\u034C\u035C" +
            "\uD800\uDC83\uD800" + // Random Surrogate Pairs 
            "\uDC80\uD802\uDD00";

    for each (var item in items) {
      item.setProperty(SBProperties.artistName, SBProperties.artistName + unicodeSample);
      item.setProperty(SBProperties.albumName, SBProperties.albumName + unicodeSample);
      item.setProperty(SBProperties.trackName, SBProperties.trackName + unicodeSample);
      // TODO expand 
    }
    
    // While we're at it, confirm that metadata can only be written when allowed via prefs
    var prefSvc = Cc["@mozilla.org/preferences-service;1"]
                  .getService(Ci.nsIPrefBranch);
    var oldWritingEnabledPref = prefSvc.getBoolPref("songbird.metadata.enableWriting");
    prefSvc.setBoolPref("songbird.metadata.enableWriting", false);
    try {
      startMetadataJob(items, Components.interfaces.sbIMetadataJob.JOBTYPE_WRITE);
      // This line should not be reached, as startMetadataJob should throw NS_ERROR_NOT_AVAILABLE
      throw new Error("MetadataJobManager does not respect enableWriting pref!");
    } catch (e) {
      if (Components.lastResult != Components.results.NS_ERROR_NOT_AVAILABLE) {
        throw new Error("MetadataJobManager does not respect enableWriting pref!");
      }
    }
    prefSvc.setBoolPref("songbird.metadata.enableWriting", true);
    job = startMetadataJob(items, Components.interfaces.sbIMetadataJob.JOBTYPE_WRITE);
    prefSvc.setBoolPref("songbird.metadata.enableWriting", oldWritingEnabledPref); 
    
    // Wait for writing to complete before continuing
    job.setObserver(new MetadataJobObserver(function onReadComplete(aSubject, aTopic, aData) {
      assertEqual(aTopic, "complete");
      assertTrue(aSubject.completed);
      job.removeObserver();
      
      /////////////////////////////////////////////////////
      // Now reimport and confirm that the write went ok //
      /////////////////////////////////////////////////////
      library.clear();
      items = importFilesToLibrary(urls, library);
      assertEqual(items.length, urls.length);
      job = startMetadataJob(items, Components.interfaces.sbIMetadataJob.JOBTYPE_READ);

      // Wait for reading to complete before continuing
      job.setObserver(new MetadataJobObserver(function onSecondReadComplete(aSubject, aTopic, aData) {
        assertEqual(aTopic, "complete");
        assertTrue(aSubject.completed);
        job.removeObserver();
        
        for each (var item in items) {
          assertEqual(item.getProperty(SBProperties.artistName), SBProperties.artistName + unicodeSample);
          assertEqual(item.getProperty(SBProperties.albumName), SBProperties.albumName + unicodeSample);
          assertEqual(item.getProperty(SBProperties.trackName), SBProperties.trackName + unicodeSample);
          // TODO expand 
        }
       
        // We're done, so kill all the temp files
        testFolder.remove(true);
        job = null;
        gTest = null;    
        testFinished();
      }));
    }));
  }));
  testPending();
}


/**
 * Copy the given folder to tempName, returning an nsIFile
 * for the new location
 */
function getCopyOfFolder(folder, tempName) {
  assertNotEqual(folder, null);
  folder.copyTo(folder.parent, tempName);
  folder = folder.parent;
  folder.append(tempName);
  assertEqual(folder.exists(), true);
  return folder;
}


/**
 * Get an array of all media files below the given folder
 */
function getMediaFilesInFolder(folder) {
  var scan = Cc["@songbirdnest.com/Songbird/FileScan;1"]
               .createInstance(Ci.sbIFileScan);
  var query = Cc["@songbirdnest.com/Songbird/FileScanQuery;1"]
               .createInstance(Ci.sbIFileScanQuery);
  query.setDirectory(folder.path);
  query.setRecurse(true);
  
  for each (var extension in gSupportedFileExtensions) {
    query.addFileExtension(extension);
  }

  scan.submitQuery(query);

  log("Scanning...");

  while (query.isScanning()) {
    sleep(1000);
  }
  
  assertEqual(query.getFileCount() > 0, true);
  var urls = query.getResultRangeAsURIs(0, query.getFileCount() - 1);

  return urls;
}


/**
 * Add files to a library, returning media items
 */
function importFilesToLibrary(files, library) {
  var items = library.batchCreateMediaItems(files, null, true);
  assertEqual(items.length, files.length);
  var jsItems = [];
  for (var i = 0; i < items.length; i++) {
    jsItems.push(items.queryElementAt(i, Ci.sbIMediaItem));
  }
  return jsItems;
}


/**
 * Get a metadata job for the given items
 */
function startMetadataJob(items, type) {
  var array = Components.classes["@mozilla.org/array;1"]
                        .createInstance(Components.interfaces.nsIMutableArray);
  for each (var item in items) {
    array.appendElement(item, false);
  }                     
  manager = Components.classes["@songbirdnest.com/Songbird/MetadataJobManager;1"]
                      .getService(Components.interfaces.sbIMetadataJobManager);
  return manager.newJob(array, 5, type);
}

