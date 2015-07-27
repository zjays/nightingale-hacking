if (typeof(Cc) == "undefined")
  var Cc = Components.classes;
if (typeof(Ci) == "undefined")
  var Ci = Components.interfaces;
if (typeof(Cu) == "undefined")
  var Cu = Components.utils;

Cu.import("resource://app/jsmodules/StringUtils.jsm");
Cu.import("resource://app/jsmodules/sbProperties.jsm");
Cu.import("resource://app/jsmodules/sbLibraryUtils.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

if (typeof(songbirdMainWindow) == "undefined")
  var songbirdMainWindow = Cc["@mozilla.org/appshell/window-mediator;1"]
      .getService(Ci.nsIWindowMediator)
      .getMostRecentWindow("Songbird:Main").window;

if (typeof(gBrowser) == "undefined")
  var gBrowser = Cc["@mozilla.org/appshell/window-mediator;1"]
      .getService(Ci.nsIWindowMediator)
      .getMostRecentWindow("Songbird:Main").window.gBrowser;

#ifdef METRICS_ENABLED
if (typeof(gMetrics) == "undefined")
  var gMetrics = Cc["@songbirdnest.com/Songbird/Metrics;1"]
        .createInstance(Ci.sbIMetrics);
#endif

function flushDisplay() {
  if (typeof(Components) == "undefined")
    return;
  while ((typeof(Components) != "undefined") && 
      Components.classes["@mozilla.org/thread-manager;1"].getService()
      .currentThread.hasPendingEvents())
  {
    Components.classes["@mozilla.org/thread-manager;1"].getService()
        .currentThread.processNextEvent(true);
  }
}

var SongkickPartnerCode = "p=" + 
   (new SBStringBundle("chrome://concerts/content/config.properties"))
    .get('SongkickPartnerCode');

var letterIndices = ['Z','Y','X','W','V','U','T','S','R','Q','P','O','N','M',
                     'L','K','J','I','H','G','F','E','D','C','B','A','#'];

if (typeof ConcertTicketing == 'undefined') {
  var ConcertTicketing = {
    QueryInterface : XPCOMUtils.generateQI([Ci.sbISongkickConcertCountCallback])
  };
}

ConcertTicketing.unload = function() {
  this.abortDrawing = true;
  this.skSvc.drawingLock = false;
  Components.classes["@songbirdnest.com/Songbird/Concerts/Songkick;1"]
    .getService(Components.interfaces.sbISongkick)
    .unregisterDisplayCallback();
}

ConcertTicketing.init = function() {
  // Set the tab title
  var servicePaneStr = Cc["@mozilla.org/intl/stringbundle;1"]
            .getService(Ci.nsIStringBundleService)
            .createBundle("chrome://concerts/locale/overlay.properties");
  document.title = servicePaneStr.GetStringFromName("servicePane.Name");

  var self = this;

  if (typeof(Ci.sbIMediacoreManager) != "undefined")
    ConcertTicketing.newMediaCore = true;
  else
    ConcertTicketing.newMediaCore = false;

  this.dateFormat = "%a, %b %e, %Y";
  if (navigator.platform.substr(0, 3) == "Win")
    this.dateFormat = "%a, %b %#d, %Y";

  this.Cc = Components.classes;
  this.Ci = Components.interfaces;

  // Load the iframe
  this.iframe = document.getElementById("concert-listings");

  // Set a pointer to our document so we can update it later as needed
  this._browseDoc = this.iframe.contentWindow.document;
  
  // Set our string bundle for localised string lookups
  this._strings = document.getElementById("concerts-strings");

  // Apply styles to the iframe
  var headNode = this._browseDoc.getElementsByTagName("head")[0];
  var cssNode = this._browseDoc.createElementNS(
      "http://www.w3.org/1999/xhtml", "html:link");
  cssNode.type = 'text/css';
  cssNode.rel = 'stylesheet';
  cssNode.href = 'chrome://songbird/skin/html.css';
  cssNode.media = 'screen';
  headNode.appendChild(cssNode);

  cssNode = this._browseDoc.createElementNS(
      "http://www.w3.org/1999/xhtml", "html:link");
  cssNode.type = 'text/css';
  cssNode.rel = 'stylesheet';
  cssNode.href = 'chrome://concerts/skin/browse.css';
  cssNode.media = 'screen';
  headNode.appendChild(cssNode);

  // Get the Songkick XPCOM service
  this.skSvc = Cc["@songbirdnest.com/Songbird/Concerts/Songkick;1"]
    .getService(Ci.sbISongkick);
  
  // Set the checked state for the filter checkbox
  this.filterLibraryArtists = Application.prefs
      .getValue("extensions.concerts.filterLibraryArtists", true);
  
  // Get pref to display play link or not
  this.showPlayLink = Application.prefs
      .getValue("extensions.concerts.showplaylink", false);
  this.showGCal = Application.prefs
      .getValue("extensions.concerts.showgcal", false);

  // Set the last saved concert count
  this.skSvc.getConcertCount(
      this.filterLibraryArtists, this);

#ifdef METRICS_ENABLED
  gMetrics.metricsInc("concerts", "servicepane.clicked", "");
#endif
  // Register our UI display callback with the Songkick object
  if (!this.skSvc.hasDisplayCallback()) {
    var displayCB = new this.displayCallback(this);
    displayCB.wrappedJSObject = displayCB;
    this.skSvc.registerDisplayCallback(displayCB);
  }

  if (Application.prefs.get("extensions.concerts.firstrun").value) {
    // Load the first run page in the deck
    var deck = document.getElementById("concerts-deck");
    deck.setAttribute("selectedIndex", 1);
    ConcertOptions.init();
  } else {
    if (this.skSvc.concertRefreshRunning) {
      // If the data refresh is already happening, switch to the progress
      // Switch the deck to the loading page
      var deck = document.getElementById("concerts-deck");
      deck.setAttribute("selectedIndex", 2);
      var label = document.getElementById("loading-label");
      var progressbar = document.getElementById("loading-progress");
      label.value = this.skSvc.progressString;
      progressbar.value = this.skSvc.progressPercentage;
      this.waitForRefreshToFinish(this);
    } else {
      // Otherwise populate the fields in the options page, and continue
      // to the listing view
      ConcertOptions.init();
      this.browseConcerts(this);
    }
  }
}

ConcertTicketing.onConcertCountEnd = function(aConcertCount) {
  this.lastConcertCount = aConcertCount;

  // Attempt to update the count element if it exists.
  var numConcertsNode = this._browseDoc.getElementById("num-concerts");
  if (numConcertsNode) {
      var bundle = new SBStringBundle(
          "chrome://concerts/locale/overlay.properties");
      var numConcertsStr = bundle.formatCountString("concertsShown",
                                                    aConcertCount,
                                                    [ aConcertCount ],
                                                    "foo");
    numConcertsNode.innerHTML = numConcertsStr;
  }
}

ConcertTicketing.createFooter = function(aConcertCount) {
    if (this._browseDoc.getElementById("ft") != null) {
        this._browseDoc.removeChild(this._browseDoc.getElementById("ft"));
    }
    if (aConcertCount > 0) {
        var ft = this.createBlock("ft");
        ft.id = "ft";
        var poweredBy = this.createBlock("poweredBy");
        var url = this.skSvc.providerURL();
        var city = Application.prefs.getValue("extensions.concerts.city", 0);
        if (city != 0)
            url += "?" + SongkickPartnerCode + "&user_location=" + city;
        poweredBy.innerHTML = this._strings.getString("poweredBy") + 
            " <a target='_blank' href='" + url + "'>Songkick</a>. "
            + this._strings.getString("tosPrefix") + 
            " <a target='_blank' href='http://www.songkick.com/info/terms/?" +
            SongkickPartnerCode + "'>" +
            this._strings.getString("tosLink") + "</a>.";
        ft.appendChild(poweredBy);

        var lastUpdated = this.createBlock("lastUpdated");
        var ts = parseInt(1000*
                Application.prefs.getValue("extensions.concerts.lastupdated", 0));
        if (ts > 0) {
            var dateString =
                new Date(ts).toLocaleFormat("%a, %b %e, %Y at %H:%M.");
            lastUpdated.innerHTML = this._strings.getString("lastUpdated") +
                " <span class='date'>" + dateString + "</span>";
            ft.appendChild(lastUpdated);
        }
        this._bodyNode.appendChild(ft);
    }
}

ConcertTicketing.waitForRefreshToFinish = function(self) {
  if (Components.classes["@songbirdnest.com/Songbird/Concerts/Songkick;1"]
    .getService(Components.interfaces.sbISongkick).concertRefreshRunning)
  {
    setTimeout(this.waitForRefreshToFinish(self), 100);
  } else {
    self.browseConcerts(self);
  }

}

ConcertTicketing.editLocation = function() {
  // Save our current selected deck page, so if the user cancels we can
  // switch back to it
  var deck = document.getElementById("concerts-deck");
  deck.setAttribute("previous-selected-deck", deck.selectedIndex);
  
  // Hide the stuff we don't want to display
  document.getElementById("pref-library").style.visibility = "hidden";
  document.getElementById("library-ontour-box").style.visibility ="collapse";

  // Not strictly required, but in the event the user hit cancel, we
  // probably want to reset to their preferred location
  ConcertOptions.init();

  // Make the cancel button visible (it's hidden by default for first run)
  var cancel = document.getElementById("pref-cancel-button");
  cancel.style.visibility="visible";

  // Switch to the location edit deck page
  deck.setAttribute("selectedIndex", 1);
}

ConcertTicketing.displayCallback = function(ticketingObj) {
  this.label = document.getElementById("loading-label"),
  this.progressbar = document.getElementById("loading-progress");
  this.concertTicketing = ticketingObj;
},
ConcertTicketing.displayCallback.prototype = {
  loadingMessage : function(str) {
    this.label.value = str;
  },
  loadingPercentage : function(pct) {
    this.progressbar.value = pct;
  },
  timeoutError : function() {
    songbirdMainWindow.Concerts.updateConcertCount(0);
    var deck = document.getElementById("concerts-deck");
    deck.setAttribute("selectedIndex", 4);
  },
  showListings : function() {
    this.concertTicketing.browseConcerts(this.concertTicketing);
  },
  updateConcertCount : function() {
    songbirdMainWindow.Concerts.updateConcertCount()
  },
  alert : function(str) { window.alert(str); },
}

// Initiate a synchronous database refresh.  This can only be triggered after
// first run, or if the user goes and changes their location
ConcertTicketing.loadConcertData = function(city) {
  // Switch the deck to the loading page
  var deck = document.getElementById("concerts-deck");
  deck.setAttribute("selectedIndex", 2);

  // First run is over
  if (Application.prefs.getValue("extensions.concerts.firstrun", false)) {
    // toggle first-run
    Application.prefs.setValue("extensions.concerts.firstrun", false);

    // setup the smart playlist
    songbirdMainWindow.Concerts._setupOnTourPlaylist();
  }

  // Load the new data
  var ret = this.skSvc.refreshConcerts(false, city);
  songbirdMainWindow.Concerts.updateConcertCount();
  if (!ret)
    this.browseConcerts(this);
}

ConcertTicketing.showNoConcerts = function() {
  var deck = document.getElementById("concerts-deck");
  deck.setAttribute("selectedIndex", 3);

  deck = document.getElementById("no-results-deck");
  var easterEgg = Application.prefs.getValue("extensions.concerts.epic", 0);
  if (easterEgg == "9x6") {
    deck.setAttribute("selectedIndex", 1);
    var label = document.getElementById("epic-city");
    label.value += this.skSvc.getLocationString(this.pCountry, this.pState,
        this.pCity);
    
    // Set the actual button text
    var button = document.getElementById("noresults-seeallconcerts-city-e");
    button.label = this._strings.getString("seeAllConcerts") + " " +
      this.skSvc.getCityString(this.pCity);
  } else {
    var label;
    var button;
    if (easterEgg == "54") {
      // no concerts found, period
      deck.setAttribute("selectedIndex", 0);
      label = document.getElementById("noresults-city");
      label.value = this._strings.getString("yourCitySucks") +
        " " + this.skSvc.getLocationString(this.pCountry,
            this.pState, this.pCity);
      button = document.getElementById("noresults-seeallconcerts-city");
      button.style.visibility = "hidden";
    } else {
      // no library artist concerts found, select the right deck
      deck.setAttribute("selectedIndex", 0);

      // show error messages
      // "Well that's lame"
      label = document.getElementById("noresults-city-1");
      label.value = this._strings.getString("noLibLame");

      // "None of the artists in your library are touring..."
      var city = " " + this.skSvc.getCityString(this.pCity);
      // ugly hack because SF Bay Area is the only one that needs "the"
      // in front of it since it's not a proper city name.  This was
      // causing problems for Babelzilla translators, so I've
      // hardcoded it to assume that if you're using SF Bay Area, then
      // you're also going to get it in English
      if (this.pCity == 26330)
        city = " the " + city;
      label = document.getElementById("noresults-city-2");
      label.value = this._strings.getString("noLibArtistsTouring") +
        city + ".";

      // "If that changes..."
      label = document.getElementById("noresults-city-3");
      label.value = this._strings.getString("noLibArtistsTouring2");
      label = document.getElementById("noresults-city-4");
      label.value = this._strings.getString("noLibArtistsTouring3");
      
      // Set the actual button text
      button = document.getElementById("noresults-seeallconcerts-city");
      button.label = this._strings.getString("seeAllConcerts") + city;
    }
  }
}

ConcertTicketing.showTimeoutError = function() {
  var deck = document.getElementById("concerts-deck");
  deck.setAttribute("selectedIndex", 4);
}

ConcertTicketing.openCityPage = function() {
  var url = Application.prefs.getValue("extensions.concerts.citypage", "");
  gBrowser.loadOneTab(url + "?" + SongkickPartnerCode);
}

ConcertTicketing.openProviderPage = function() {
  var url;
  if (typeof(this.skSvc) != "undefined")
    url = this.skSvc.providerURL();
  else
    url = Cc["@songbirdnest.com/Songbird/Concerts/Songkick;1"]
        .getService(Ci.sbISongkick).providerURL();

  var city = Application.prefs.getValue("extensions.concerts.city", 0);
  if (city != 0)
    url += "?" + SongkickPartnerCode + "&user_location=" + city;
  gBrowser.loadOneTab(url);
}

/***************************************************************************
 * Called when the user clicks "Play" next to the main artist name in the
 * artist grouping view of upcoming concerts
 ***************************************************************************/
ConcertTicketing.playArtist = function(e) {
  var artistName = this.getAttribute("artistName");
  
#ifdef METRICS_ENABLED
  gMetrics.metricsInc("concerts", "browse.view.artist.playartist", "");
#endif
  var list = songbirdMainWindow.Concerts.touringPlaylist;
  var view = list.createView();
  var cfs = view.cascadeFilterSet;
  
  cfs.appendSearch(["*"], 1);
  cfs.appendFilter(SBProperties.genre);
  cfs.appendFilter(SBProperties.artistName);
  cfs.appendFilter(SBProperties.albumName);

  cfs.clearAll();
  cfs.set(2, [artistName], 1);

  if (ConcertTicketing.newMediaCore)
    Cc["@songbirdnest.com/Songbird/Mediacore/Manager;1"]
      .getService(Ci.sbIMediacoreManager)
      .sequencer.playView(view, 0);
  else
    Cc['@songbirdnest.com/Songbird/PlaylistPlayback;1']
      .getService(Ci.sbIPlaylistPlayback)
      .sequencer.playView(view, 0);
}

/***************************************************************************
 * Scrolls the iframe to the selected index block when the user clicks on
 * one of the letter or month index links in the chrome
 ***************************************************************************/
ConcertTicketing.indexJump = function(e) {
  var iframe = document.getElementById("concert-listings");
  this._browseDoc = iframe.contentWindow.document;

  var anchor;
  if (this.id.indexOf("concerts-nav-letter-") >= 0) {
    var letter = this.id.replace("concerts-nav-letter-", "");
    anchor = this._browseDoc.getElementById("indexLetter-" + letter);
  } else {
    var dateComponent = this.id.split("-")[3];
    anchor = this._browseDoc.getElementById("indexDate-" + dateComponent);
  }
  if (anchor) {
    var aLeft = anchor.offsetLeft;
    var aTop = anchor.offsetTop;
    iframe.contentWindow.scrollTo(0, aTop);
  }
}


ConcertTicketing.browseConcerts = function(ticketingObj) {
  if (this.skSvc.drawingLock)
    return;
  this.skSvc.drawingLock = true;
  this.abortDrawing = false;

  // Switch to the listing view
  var deck = document.getElementById("concerts-deck");
  deck.setAttribute("selectedIndex", 0);

  /*
   * bug 13347 - disabling this in favour of going to 'edit location' page
   * instead
  regionLabel.addEventListener("click",
      self.ConcertTicketing.openCityPage, false);
  */

  this._bodyNode = this._browseDoc.getElementsByTagName("body")[0];

  // Clear any existing results
  while (this._bodyNode.firstChild) {
    this._bodyNode.removeChild(this._bodyNode.firstChild);
  }

  /*
  // Add the Songkick logo
  var skDiv = this.createBlock("powered-by-songkick");
  skDiv.style.width = "100%";
  skDiv.style.textAlign = "right";
  var skImg = this._browseDoc.createElement("img");
  skImg.src = "chrome://concerts/content/songkick.png";
  skImg.className = "clickable";
  skImg.addEventListener("click", this.openProviderPage, false);
  skDiv.appendChild(skImg);
  this._bodyNode.appendChild(skDiv);
  */
  
  var easterEgg = Application.prefs.getValue("extensions.concerts.epic", 0);
  if (easterEgg == "doctorwho") {
    ConcertTicketing.showTimeoutError();
    return;
  }

  var doc = this._browseDoc;
  var str = this._strings;

  // Add the header block
  var headerDiv = this.createBlock("header");
  var concertsImage = doc.createElement("img");
  concertsImage.src = "chrome://concerts/skin/Concerts.png";
  concertsImage.id = "concerts-logo";
  headerDiv.appendChild(concertsImage);
  var songkickImage = doc.createElement("img");
  songkickImage.src = "chrome://concerts/content/songkick-logo-concerts-home.png";
  songkickImage.id = "songkick-logo";
  songkickImage.className = "clickable";
  songkickImage.addEventListener("click", this.openProviderPage, false);
  headerDiv.appendChild(songkickImage);
  this._bodyNode.appendChild(headerDiv);

  // Add the subheader block
  var subHeaderDiv = this.createBlock("sub-header");
  var numConcertsShownDiv = this.createBlock("num-concerts", true);
  numConcertsShownDiv.id = "num-concerts";
  subHeaderDiv.appendChild(numConcertsShownDiv); 
  this.skSvc.getConcertCount(this.filterLibraryArtists, this);
  songbirdMainWindow.Concerts.updateConcertCount();

  
  var locationChangeDiv = this.createBlock("location", true);
  subHeaderDiv.appendChild(locationChangeDiv);
  var cityName = this.createBlock("location-city", true);
  this.pCountry = Application.prefs.getValue("extensions.concerts.country",0);
  this.pState = Application.prefs.getValue("extensions.concerts.state",0);
  this.pCity = Application.prefs.getValue("extensions.concerts.city", 0);
  var locationString = this.skSvc.getLocationString(this.pCountry,
                                                    this.pState,
                                                    this.pCity);
  cityName.appendChild(doc.createTextNode(locationString));
  locationChangeDiv.appendChild(cityName);
  var changeLocation = this.createBlock("location-change", true);
  changeLocation.appendChild(doc.createTextNode(str.getString("changeLoc")));
  locationChangeDiv.appendChild(changeLocation);
  changeLocation.addEventListener("click", ConcertTicketing.editLocation,
                                  false);

  var filterDiv = this.createBlock("filter", true);
  var checkbox = doc.createElement("input");
  checkbox.setAttribute("type", "checkbox");
  if (this.filterLibraryArtists)
    checkbox.setAttribute("checked", true);
  checkbox.addEventListener("click", function(e)
      { ConcertTicketing.changeFilter(false); }, false);
  filterDiv.appendChild(checkbox);
  filterDiv.appendChild(doc.createTextNode(str.getString("filter")));
  subHeaderDiv.appendChild(filterDiv);

  var clearDiv = this.createBlock("sub-header-empty");
  clearDiv.style.clear = "both";
  subHeaderDiv.appendChild(clearDiv);
  this._bodyNode.appendChild(subHeaderDiv);


  var groupBy = Application.prefs.getValue("extensions.concerts.groupby",
                                           "artist");
  // Add the nav block - the View/Concerts/Artists selector
  var navDiv = this.createBlock("concerts-nav");
  var viewSpan = this.createBlock("concerts-nav-view", true);
  viewSpan.appendChild(doc.createTextNode(str.getString("navView")));

  var datesSpan = this.createBlock("concerts-nav-concerts", true);
  var artistsSpan = this.createBlock("concerts-nav-artists", true);
  if (groupBy == "artist") {
    artistsSpan.className += " concerts-nav-selected";
    datesSpan.addEventListener("mouseover", function(e)
        {
          datesSpan.className += " concerts-nav-hover";
        }, false);
    datesSpan.addEventListener("mouseout", function(e)
        {
          datesSpan.className = datesSpan.className.replace(
                                      " concerts-nav-hover", "");
        }, false);
    datesSpan.addEventListener("click", function(e)
        {
          ConcertTicketing.groupBy("date");
        }, false);
  } else {
    datesSpan.className += " concerts-nav-selected";
    artistsSpan.addEventListener("mouseover", function(e)
        {
          artistsSpan.className += " concerts-nav-hover";
        }, false);
    artistsSpan.addEventListener("mouseout", function(e)
        {
          artistsSpan.className = artistsSpan.className.replace(
                                      " concerts-nav-hover", "");
        }, false);
    artistsSpan.addEventListener("click", function(e)
        {
          ConcertTicketing.groupBy("artist");
        }, false);
   }

  datesSpan.appendChild(doc.createTextNode(str.getString("navDates")));
  artistsSpan.appendChild(doc.createTextNode(str.getString("navArtists")));
  navDiv.appendChild(viewSpan);
  navDiv.appendChild(datesSpan);
  navDiv.appendChild(artistsSpan);

  var indexDiv = this.createBlock("concerts-nav-index");
  if (groupBy == "artist") {
    // group by Artist Names
    // Add the #,A-Z index letters
    for (var i in letterIndices) {
      var l = letterIndices[i];
      var letterSpan = this.createBlock("concerts-nav-letter", true);
      letterSpan.id = "concerts-nav-letter-" + l;
      letterSpan.appendChild(doc.createTextNode(l));
      indexDiv.appendChild(letterSpan);
    }
  } else {
    // group by Concert Dates
    var myDate = new Date();
    myDate.setDate(1);
    var dates = [];
    for (let i=0; i<6; i++) {
      var mon = myDate.getMonth();
      var year = myDate.getFullYear();
      var dateStr = myDate.toLocaleFormat("%b %Y");
      var dateSpan = this.createBlock("concerts-nav-date", true);
      dateSpan.appendChild(doc.createTextNode(dateStr));
      dateSpan.id = "concerts-nav-date-" + mon + year;
      myDate.setMonth(mon+1);
      dates.push(dateSpan);
    }
    for (var i in dates.reverse()) {
      indexDiv.appendChild(dates[i], null);
    }
  }
  navDiv.appendChild(indexDiv);
  clearDiv = this.createBlock("concerts-nav-empty");
  clearDiv.style.clear = "both";
  navDiv.appendChild(clearDiv);
  this._bodyNode.appendChild(navDiv);
  
  flushDisplay();

  if (groupBy == "artist")
    this.browseArtists();
  else
    this.browseDates();

  // Debug
  /*
  var html = this._bodyNode.innerHTML;
  var textbox = this._browseDoc.createElement("textarea");
  textbox.width=50;
  textbox.height=50;
  this._bodyNode.appendChild(textbox);
  textbox.value = html;
  */
  
  this.skSvc.drawingLock = false;
}

ConcertTicketing.browseDates = function() {
  this._isWaitingOnBrowseDates = true;
  this.skSvc.concertEnumerator(
    "date", this.filterLibraryArtists, this);
}

ConcertTicketing.onBrowseDatesReady = function(concerts) {
  var lastDate = "";
  var dateBlock = null;
  var concertTable = null;
  var venueConcertBlock = null;
  var concertsShown = 0;

  var contentsNode = this._browseDoc.getElementById("concerts-contents");

  var today = new Date();
  var todayMon = today.getMonth();
  var todayDate = today.getDate();
  var todayYear = today.getFullYear();

#ifdef METRICS_ENABLED
  gMetrics.metricsInc("concerts", "browse.view.date", "");
#endif
  while (concerts.hasMoreElements()) {
    if (this.abortDrawing)
      return;
    var concert = concerts.getNext().QueryInterface(Ci.sbISongkickConcertInfo);

    var thisDateObj = new Date(concert.ts * 1000);
    var thisMon = thisDateObj.getMonth();
    var thisYear = thisDateObj.getFullYear();
    var thisIndex = thisMon + "" + thisYear;
    var thisDate = thisYear + '-' + thisMon + '-' + thisDateObj.getDate();

    // Don't show past concerts
    if (((thisMon < todayMon) && (thisYear <= todayYear)) ||
      (thisMon == todayMon && thisDateObj.getDate() < todayDate)) {
      continue;
    }

    var doc = this._browseDoc;
    var dateIndex = doc.getElementById("concerts-nav-date-" + thisIndex);
    if (dateIndex == null) {
      continue;
    }
    dateIndex.className += " concerts-nav-date-link";
    dateIndex.addEventListener("mouseover", function(e)
        {
          var el = e.currentTarget;
          el.className += " concerts-nav-hover";
        }, false);
    dateIndex.addEventListener("mouseout", function(e)
        {
          var el = e.currentTarget;
          el.className = el.className.replace(" concerts-nav-hover", "");
        }, false);
    dateIndex.addEventListener("click", ConcertTicketing.indexJump, false);

    if (thisDate != lastDate) {
      // Create the block for concert listings of the same letter index
      var dateDiv = this.createBlock("indexDiv");

      // Create the letter index block
      var dateIndexContainer = this.createDateBlock(thisDateObj);

      // Create the actual concert listing block for this date
      dateBlock = this.createBlock("concertListing");
      dateBlock.className += " concertListingDate";

      // Create a new concert listing block
      var concertTableBlock = this.createBlock("artistBlock");
      
      // Create the table for concerts
      concertTable = this.createTableDateView();
      concertTableBlock.appendChild(concertTable);

      // Assemble the pieces together
      dateBlock.appendChild(concertTableBlock);
      dateDiv.appendChild(dateIndexContainer);
      dateDiv.appendChild(dateBlock);
      contentsNode.appendChild(dateDiv);
      flushDisplay();
    }
    
    // Create the table row representing this concert
    var thisConcert = this.createRowDateView(concert);
    concertTable.appendChild(thisConcert);

    lastDate = thisDate;
    concertsShown++;
  }
  
  if (concertsShown == 0) {
    if (Application.prefs.getValue("extensions.concerts.networkfailure",
          false))
      ConcertTicketing.showTimeoutError();
    else
      ConcertTicketing.showNoConcerts();
  }

  this.createFooter(concertsShown);
  return concertsShown;
}

// XXX KREEGER + STEVEL == THIS IS DEAD CODE ///////////////////////////////////
ConcertTicketing.browseVenues = function(concerts) {
  var lastLetter = "";
  var lastVenue = "";
  var concertBlock = null;
  var venueConcertBlock = null;
  var thisMainVenueAnchor = null;

  var concertsShown = 0;
  while (concerts.hasMoreElements()) {
    var concert = concerts.getNext().wrappedJSObject;

    var thisLetter = concert.venue[0].toUpperCase();
    var letterIdx = document.getElementById("letter-index-" + thisLetter);
    letterIdx.className = "index-letter text-link";
    letterIdx.addEventListener("click", ConcertTicketing.indexJump, false);
    if (thisLetter != lastLetter) {
      // Create the block for concert listings of the same letter index
      var letterDiv = this.createBlock("indexDiv");

      // Create the letter index block
      var letterIndexContainer = this.createLetterBlock(thisLetter);

      // Create the actual concert listing block for this letter
      concertBlock = this.createBlock("concertListing");
      concertBlock.className += " concertListingLetter";

      // Assemble the pieces together
      letterDiv.appendChild(letterIndexContainer);
      letterDiv.appendChild(concertBlock);
      this._bodyNode.appendChild(letterDiv);
    }
    if (concert.venue != lastVenue) {
      // Create a new venue block
      var venueBlock = this.createBlock("artistBlock");

      // Create the main venue title
      var venueName = this.createBlock("mainArtistName");
      thisMainVenueAnchor = this._browseDoc.createElement("a");
      thisMainVenueAnchor.setAttribute("target", "_blank");
      var venueNameText = this._browseDoc.createTextNode(concert.venue);
      thisMainVenueAnchor.appendChild(venueNameText);
      venueName.appendChild(thisMainVenueAnchor);
      venueBlock.appendChild(venueName);

      // Create the table for concerts
      venueConcertBlock = this.createTableVenueView();
      venueBlock.appendChild(venueConcertBlock);

      // Attach the venue block to the concert listing block
      concertBlock.appendChild(venueBlock);
    }
    
    // Create the table row representing this concert
    var thisConcert = this.createRowVenueView(concert);
    venueConcertBlock.appendChild(thisConcert);

    lastLetter = thisLetter;
    lastVenue = concert.venue;
    
    concertsShown++;
  }
  
  if (concertsShown == 0) {
    if (Application.prefs.getValue("extensions.concerts.networkfailure",
          false))
      ConcertTicketing.showTimeoutError();
    else
      ConcertTicketing.showNoConcerts();
  }

  return concertsShown;
}
////////////////////////////////////////////////////////////////////////////////

ConcertTicketing.browseArtists = function() {
  this._isWatingOnBrowseArtists = true;
  this.skSvc.artistConcertEnumerator(
      this.filterLibraryArtists, this);
}

ConcertTicketing.onBrowseArtistsReady = function(concerts) {
  var lastLetter = "";
  var lastArtist = "";
  var concertBlock = null;
  var artistConcertBlock = null;
  var thisMainArtistAnchor = null;
  var concertsShown = 0;
  
  var contentsNode = this._browseDoc.getElementById("concerts-contents");
  
  var today = new Date();
  var todayMon = today.getMonth();
  var todayDate = today.getDate();
  var todayYear = today.getFullYear();

#ifdef METRICS_ENABLED
  gMetrics.metricsInc("concerts", "browse.view.artist", "");
#endif
  while (concerts.hasMoreElements()) {
    if (this.abortDrawing)
      return;
    var concert = concerts.getNext().QueryInterface(Ci.sbISongkickConcertInfo);

    var thisDateObj = new Date(concert.ts * 1000);
    var thisMon = thisDateObj.getMonth();
    var thisYear = thisDateObj.getFullYear();
    var thisIndex = thisMon + "" + thisYear;
    var thisDate = thisYear + '-' + thisMon + '-' + thisDateObj.getDate();

    // Don't show past concerts
    if (((thisMon < todayMon) && (thisYear <= todayYear)) ||
      (thisMon == todayMon && thisDateObj.getDate() < todayDate))
    {
      continue;
    }

    var artistname = unescape(decodeURIComponent(concert.artistname));
    var thisLetter = artistname[0].toUpperCase();
    if (thisLetter < 'A' || thisLetter > 'Z')
      thisLetter = '#';

    var doc = this._browseDoc;
    var letterIdx = doc.getElementById("concerts-nav-letter-" + thisLetter);
    if (letterIdx == null) {
      continue;
    }
    letterIdx.className += " concerts-nav-letter-link";
    letterIdx.addEventListener("mouseover", function(e)
        {
          var el = e.currentTarget;
          el.className += " concerts-nav-hover";
        }, false);
    letterIdx.addEventListener("mouseout", function(e)
        {
          var el = e.currentTarget;
          el.className = el.className.replace(" concerts-nav-hover", "");
        }, false);
    letterIdx.addEventListener("click", ConcertTicketing.indexJump, false);

    if (thisLetter != lastLetter) {
      // Create the block for concert listings of the same letter index
      var letterDiv = this.createBlock("indexDiv");

      // Create the letter index block
      var letterIndexContainer = this.createLetterBlock(thisLetter);

      // Create the actual concert listing block for this letter
      concertBlock = this.createBlock("concertListing");
      concertBlock.className += " concertListingLetter";

      // Assemble the pieces together
      letterDiv.appendChild(letterIndexContainer);
      letterDiv.appendChild(concertBlock);
      //this._bodyNode.appendChild(letterDiv);
      contentsNode.appendChild(letterDiv);
      flushDisplay();
    }
    if (artistname != lastArtist) {
      // Create a new artist block
      var artistBlock = this.createBlock("artistBlock");

      // Create the main concert title
      var artistNameBlock = this.createBlock("mainArtistName", true);
      thisMainArtistAnchor = this._browseDoc.createElement("a");
      this.makeLink(thisMainArtistAnchor, "", "artist.main");
      var artistNameText =
        this._browseDoc.createTextNode(artistname);
      thisMainArtistAnchor.appendChild(artistNameText);
      artistNameBlock.appendChild(thisMainArtistAnchor);
      artistBlock.appendChild(artistNameBlock);

      if (concert.libartist == 1 && this.showPlayLink) {
        // Create the play link
        var playBlock = this.createBlock("playBlock", true);
        var playLink = this._browseDoc.createElement("img");
        playLink.className = "ticketImage";
        playLink.src = "chrome://concerts/skin/icon-play.png";
        playBlock.className = "playLink";
        playBlock.appendChild(playLink);
        playBlock.setAttribute("artistName", artistname);
        playBlock.addEventListener("click", this.playArtist, false);
        artistBlock.appendChild(playBlock);
      }

      // Create the table for concerts
      artistConcertBlock = this.createTableArtistView();
      artistBlock.appendChild(artistConcertBlock);

      // Attach the artist block to the concert listing block
      concertBlock.appendChild(artistBlock);
    }
    
    // Create the table row representing this concert
    var thisConcert = this.createRowArtistView(concert,
        thisMainArtistAnchor);
    artistConcertBlock.appendChild(thisConcert);

    lastLetter = thisLetter;
    lastArtist = artistname;
    
    concertsShown++;
  }
  
  if (concertsShown == 0) {
    if (Application.prefs.getValue("extensions.concerts.networkfailure",
          false))
      ConcertTicketing.showTimeoutError();
    else
      ConcertTicketing.showNoConcerts();
  }

  this.createFooter(concertsShown);
  return concertsShown;
}

ConcertTicketing.onEnumerationStart = function() {
  // Add the loading dialog in.
  var contentsNode = this._browseDoc.createElement("div");
  contentsNode.id = "concerts-contents";
  this._bodyNode.appendChild(contentsNode);

  var strBundle = Cc["@mozilla.org/intl/stringbundle;1"]
      .getService(Ci.nsIStringBundleService)
      .createBundle("chrome://concerts/locale/songkick.properties");

  var loadingNode = this._browseDoc.createElement("div");
  loadingNode.id = "concerts-loading";
  loadingNode.innerHTML = strBundle.GetStringFromName("preparing");
  contentsNode.appendChild(loadingNode);
}

ConcertTicketing.onEnumerationEnd = function(aConcertsEnum) {
  // Remove the loading block.
  var loadingNode = this._browseDoc.getElementById("concerts-loading");
  var contentsNode = this._browseDoc.getElementById("concerts-contents");
  contentsNode.removeChild(loadingNode);

  if (this._isWatingOnBrowseArtists) {
    this._isWatingOnBrowseArtists = false;
    this.onBrowseArtistsReady(aConcertsEnum);
  }
  if (this._isWaitingOnBrowseDates) {
    this._isWaitingOnBrowseDates = false;
    this.onBrowseDatesReady(aConcertsEnum);
  }
}

// Takes a letter, and returns an index block for it
ConcertTicketing.createLetterBlock = function(letter) {
  // letter index (LHS)
  var letterIndexContainer = this.createBlock("letterIndexContainer");
  var letterIndex = this.createBlock("letterIndex");
  letterIndex.id = "indexLetter-" + letter;
  var letterIndexText = this._browseDoc.createTextNode(letter);
  letterIndex.appendChild(letterIndexText);
  letterIndexContainer.appendChild(letterIndex);
  return (letterIndexContainer);
}

ConcertTicketing.openDateLink = function(e) {
  var citypage=Application.prefs.getValue("extensions.concerts.citypage", "");
#ifdef METRICS_ENABLED
  gMetrics.metricsInc("concerts", "browse.link.datebox", "");
#endif
  if (citypage) {
    var year = this.getAttribute("year");
    var month = parseInt(this.getAttribute("month")) + 1;
    var date = this.getAttribute("date");
    gBrowser.loadOneTab(citypage + "?" + SongkickPartnerCode +
        "&d=" + year + "-" + month + "-" + date);
  }
}

// Takes a date, and returns an index block for it
ConcertTicketing.createDateBlock = function(dateObj) {
  var dateIndexContainer = this.createBlock("dateIndexContainer");
  var boxBlock = this.createBlock("dateIndex-box");
  boxBlock.addEventListener("click", ConcertTicketing.openDateLink, false);
  boxBlock.setAttribute("year", dateObj.getFullYear());
  boxBlock.setAttribute("month", dateObj.getMonth());
  boxBlock.setAttribute("date", dateObj.getDate());
  boxBlock.style.cursor = "pointer";
  var month = this.createBlock("dateIndex-month");
  var date = this.createBlock("dateIndex-date");
  var day = this.createBlock("dateIndex-day");
  dateIndexContainer.id = "indexDate-" + dateObj.getMonth() +
    dateObj.getFullYear();
  var monthText =
    this._browseDoc.createTextNode(dateObj.toLocaleFormat("%b"));
  var dateText = this._browseDoc.createTextNode(dateObj.toLocaleFormat("%d"));
  var dayText = this._browseDoc.createTextNode(dateObj.toLocaleFormat("%a"));
  month.appendChild(monthText);
  day.appendChild(dayText);
  date.appendChild(dateText);
  boxBlock.appendChild(month);
  boxBlock.appendChild(date);
  dateIndexContainer.appendChild(boxBlock);
  dateIndexContainer.appendChild(day);
  return (dateIndexContainer);
}

/***************************************************************************
 * Routines for building the actual table objects for listing individual
 * concerts within
 ***************************************************************************/
ConcertTicketing.createTableArtistView = function() {
  var table = this.createTable();
  var headerRow = this._browseDoc.createElement("tr");
  var dateCol = this.createTableHeader("tableHeaderDate", "date");
  var gCalCol = this.createTableHeader("tableHeaderGCal", "gcal");
  var artistsCol = this.createTableHeader("tableHeaderOtherArtists",
      "artists");
  var venueCol = this.createTableHeader("tableHeaderVenue", "venue");
  var ticketCol = this.createTableHeader("tableHeaderTickets", "tickets");
  headerRow.appendChild(dateCol);
  if (this.showGCal)
    headerRow.appendChild(gCalCol);
  headerRow.appendChild(artistsCol);
  headerRow.appendChild(venueCol);
  headerRow.appendChild(ticketCol);
  table.appendChild(headerRow);
  return (table);
}

ConcertTicketing.createTableVenueView = function() {
  var table = this.createTable();
  var headerRow = this._browseDoc.createElement("tr");
  var ticketCol = this.createTableHeader("tableHeaderTickets", "tickets");
  var dateCol = this.createTableHeader("tableHeaderDate", "date");
  var artistsCol = this.createTableHeader("tableHeaderArtists", "artists");
  headerRow.appendChild(ticketCol);
  headerRow.appendChild(dateCol);
  headerRow.appendChild(artistsCol);
  table.appendChild(headerRow);
  return (table);
}

ConcertTicketing.createTableDateView = function() {
  var table = this.createTable();
  var headerRow = this._browseDoc.createElement("tr");
  var gCalCol = this.createTableHeader("tableHeaderGCal", "gcal");
  var artistsCol = this.createTableHeader("tableHeaderArtists", "artists");
  var venueCol = this.createTableHeader("tableHeaderVenue", "venue");
  var ticketCol = this.createTableHeader("tableHeaderTickets", "tickets");
  if (this.showGCal)
    headerRow.appendChild(gCalCol);
  headerRow.appendChild(artistsCol);
  headerRow.appendChild(venueCol);
  headerRow.appendChild(ticketCol);
  table.appendChild(headerRow);
  return (table);
}

ConcertTicketing.createTable = function() {
  var table = this._browseDoc.createElement("table");
  table.setAttribute("cellpadding", "0");
  table.setAttribute("cellspacing", "0");
  table.setAttribute("border", "0");
  return (table);
}

// str is a name in the .properties localised strings
// className is the class to apply to the TH cell
ConcertTicketing.createTableHeader = function(str, className) {
  var col = this._browseDoc.createElement("th");
  col.className = className;
  if (str == "tableHeaderTickets" || this._strings.getString(str) == "")
    col.innerHTML = "&nbsp;";
  else {
    var colLabel =
      this._browseDoc.createTextNode(this._strings.getString(str));
    col.appendChild(colLabel);
  }
  return (col);
}
/***************************************************************************
 * Routines for building the individual concert listing rows, i.e. the table
 * row representing each individual concert for each of the 3 views
 ***************************************************************************/
ConcertTicketing.createRowArtistView = function(concert, mainAnchor) {
  var row = this._browseDoc.createElement("tr");
  var ticketCol = this.createColumnTickets(concert);
  var dateCol = this.createColumnDate(concert);
  var gcalCol = this.createColumnGCal(concert);
  var venueCol = this.createColumnVenue(concert);

  // Artists playing - logic is slightly different from the other venue &
  // date views since we don't want to show the artist that we're already
  // listed under (for the "main index"), and we need to update the main
  // index anchor href
  var otherArtistsCol = this._browseDoc.createElement("td");
  otherArtistsCol.className = "artists";
  var first = true;
  var artists = concert.artistsConcertInfo;
  var artistname = unescape(decodeURIComponent(concert.artistname));
  for (var i = 0; i < artists.length; i++) {
    var curArtist = artists.queryElementAt(i, Ci.sbISongkickArtistConcertInfo);
    let curArtistName = unescape(decodeURIComponent(curArtist.artistname));
    if (curArtistName == artistname) {
      mainAnchor.setAttribute("href",
          this.appendCityParam(curArtist.artisturl));
      continue;
    }
    if (!first) {
      otherArtistsCol.appendChild(this._browseDoc.createTextNode(", "));
    } else {
      first = false;
    }
    var anchor = this._browseDoc.createElement("a");
    this.makeLink(anchor, this.appendCityParam(curArtist.artisturl),
        "artist.other");
    var anchorLabel = this._browseDoc.createTextNode(curArtistName);
    anchor.appendChild(anchorLabel);
    otherArtistsCol.appendChild(anchor);
  }
  if (otherArtistsCol.firstChild == null)
    otherArtistsCol.innerHTML = "&nbsp;";

  row.appendChild(dateCol);
  if (this.showGCal)
    row.appendChild(gcalCol);
  row.appendChild(otherArtistsCol);
  row.appendChild(venueCol);
  row.appendChild(ticketCol);
  return (row);
}

ConcertTicketing.createRowVenueView = function(concert) {
  var row = this._browseDoc.createElement("tr");
  var ticketCol = this.createColumnTickets(concert);
  var dateCol = this.createColumnDate(concert);
  var artistsCol = this.createColumnArtists(concert);
  row.appendChild(ticketCol);
  row.appendChild(dateCol);
  row.appendChild(artistsCol);
  return (row);
}

ConcertTicketing.createRowDateView = function(concert) {
  var row = this._browseDoc.createElement("tr");
  var ticketCol = this.createColumnTickets(concert);
  var gcalCol = this.createColumnGCal(concert);
  var artistsCol = this.createColumnArtists(concert);
  var venueCol = this.createColumnVenue(concert);
  if (this.showGCal)
    row.appendChild(gcalCol);
  row.appendChild(artistsCol);
  row.appendChild(venueCol);
  row.appendChild(ticketCol);
  return (row);
}

/***************************************************************************
 * Routine for opening links - we need it as a separate routine so we can
 * do metrics reporting, in addition to just opening the link
 ***************************************************************************/
ConcertTicketing.openAndReport = function(e) {
#ifdef METRICS_ENABLED
  var metric = this.getAttribute("metric-key");
  gMetrics.metricsInc("concerts", "browse.link." + metric, "");
#endif
  gBrowser.loadOneTab(this.href);
  e.preventDefault();
  return false;
}
ConcertTicketing.makeLink = function(el, url, metric) {
  el.href = url;
  el.setAttribute("metric-key", metric);
  el.addEventListener("click", this.openAndReport, true);
}

// Adds Songbird's partner code & user location
ConcertTicketing.appendCityParam = function(url) {
  return (url + "?" + SongkickPartnerCode + "&user_location=" + this.pCity);
}
// Add's Songbird's partner code only
ConcertTicketing.appendPartnerParam = function(url) {
  return (url + "?" + SongkickPartnerCode);
}

/***************************************************************************
 * Routines for building the individual column components of each concert
 * listing table row, e.g. the "Date" column, or the "Artists" column, etc.
 ***************************************************************************/
ConcertTicketing.createColumnTickets = function(concert) {
  var ticketCol = this._browseDoc.createElement("td");
  ticketCol.className = "tickets";
  if (concert.tickets) {
    var ticketAnchor = this._browseDoc.createElement("a");
    this.makeLink(ticketAnchor, this.appendPartnerParam(concert.url),
        "tickets");
    ticketAnchor.setAttribute("title",
        this._strings.getString("tableTicketsTooltip"));
    ticketAnchor.className = "get-tickets";
    ticketAnchor.innerHTML = this._strings.getString("ticketButtonLabel");
    ticketCol.appendChild(ticketAnchor);
  } else {
    ticketCol.innerHTML = "&nbsp;"
  }

  return (ticketCol);
}

ConcertTicketing.createColumnDate = function(concert) {
  var dateCol = this._browseDoc.createElement("td");
  dateCol.className = "date";
  var dateObj = new Date(concert.ts*1000);
  var dateStr = dateObj.toLocaleFormat(this.dateFormat);
  var dateColLabel = this._browseDoc.createTextNode(dateStr);
  var citypage = Application.prefs.getValue("extensions.concerts.citypage",
      "");
  if (citypage != "") {
    var dateFormat = dateObj.toLocaleFormat("%Y-%m-%d");
    var dateAnchor = this._browseDoc.createElement("a");
    this.makeLink(dateAnchor, this.appendPartnerParam(concert.url),
        "tickets");
    dateAnchor.setAttribute("title",
        this._strings.getString("tableTicketsTooltip"));
    dateAnchor.appendChild(dateColLabel);
    dateCol.appendChild(dateAnchor);
  } else {
    dateCol.appendChild(dateColLabel);
  }

  return (dateCol);
}

ConcertTicketing.createColumnGCal = function(concert) {
  var dateObj = new Date(concert.ts*1000);
  var dateStr = dateObj.toLocaleFormat("%Y%m%d"); // T%H%M%SZ");
  var url = "http://www.google.com/calendar/event?action=TEMPLATE";
  url += "&text=" + escape(concert.title);
  url += "&details=";
  for (let artist in concert.artists) {
    url += escape(concert.artists[artist].name) + ",";
  }
  // trim the last ,
  url = url.substr(0, url.length-1);
  url += "&location=" + escape(concert.venue) + "," + escape(concert.city);
  url += "&dates=" + dateStr + "/" + dateStr;

  var gCalLinkCol = this._browseDoc.createElement("td");
  gCalLinkCol.className = "gcal";
  var gCalLink = this._browseDoc.createElement("a");
  this.makeLink(gCalLink, url, "gcal");
  gCalLink.setAttribute("title", this._strings.getString("tableGCalTooltip"));
  
  var gCalImage = this._browseDoc.createElement("img");
  gCalImage.src = "chrome://concerts/skin/gcal.png";
  gCalImage.className = "gcalImage";
  gCalLink.appendChild(gCalImage);

  gCalLinkCol.appendChild(gCalLink);

  return (gCalLinkCol);
}

ConcertTicketing.createColumnArtists = function(concert) {
  /*
   * Construct the artists playing string
   * Take the concert title, and for each artist listed as playing at this
   * concert, mask it out of the concert title.  After all the artists have
   * been masked out, test to see if the concert title consists of anything
   * other than punctuation.  If it doesn't, then the title was just a list
   * of artists - and don't display it.  If it's got non-punctuation chars,
   * then we'll classify it as a concert/festival name and list it as
   * "Festival Name with Artist1, Artist2" 
   */
  var artists = concert.artistsConcertInfo;
  var artistsCol = this._browseDoc.createElement("td");
  artistsCol.className = "artists";
  var concertTitle = concert.title;
  var first = true;
  var headlinerFound = false;
  var headlinerNode;
  for (var i = 0; i < artists.length; i++) {
    // Comma separate the list of artists
    var commaNode = null;
    if (!first) {
      commaNode = this._browseDoc.createTextNode(", ");
    }

    var curArtistConcertInfo =
      artists.queryElementAt(i, Ci.sbISongkickArtistConcertInfo);
    // Linkify the artist name
    var anchor = this._browseDoc.createElement("a");
    this.makeLink(
        anchor,
        this.appendCityParam(curArtistConcertInfo.artisturl),
        "artist.other");
    var artistName =
      unescape(decodeURIComponent(curArtistConcertInfo.artistname));
    var anchorLabel = this._browseDoc.createTextNode(artistName);
    anchor.appendChild(anchorLabel);

    // Test to see if the concert title is the exact same string as this
    // artist name - if so, then this artist is the headliner and should
    // be called out first, otherwise append the artist name/link to the
    // column
    if (artistName == concert.title) {
      headlinerFound = true;
      headlinerNode = anchor;
    } else {
      if (commaNode != null)
        artistsCol.appendChild(commaNode);
      artistsCol.appendChild(anchor);
      first = false;
    }

    // Mask it out of the concert title
    concertTitle = concertTitle.replace(artistName, "");
  }
  /*
   * XXX Taking this logic out for now - it'd work really nicely if we had
   * consistently clean concert titles, but we don't
   */
  /*
  if (!headlinerFound) {
    concertTitle = concertTitle.replace(/[\s!\.:\&;\(\)\/]/gi, "");
    if (concertTitle.length > 0) {
      var concertNode = this._browseDoc.createTextNode(concert.title +
          " featuring ");
      artistsCol.insertBefore(concertNode, artistsCol.firstChild);
    }
  } else {
    if (!first) {
      var withNode = this._browseDoc.createTextNode(" with ");
      artistsCol.insertBefore(withNode, artistsCol.firstChild);
    }
    artistsCol.insertBefore(headlinerNode, artistsCol.firstChild);
  }
  */
  if (headlinerFound) {
    if (!first) {
      var withNode = this._browseDoc.createTextNode(" with ");
      artistsCol.insertBefore(withNode, artistsCol.firstChild);
    }
    artistsCol.insertBefore(headlinerNode, artistsCol.firstChild);
  }

  return (artistsCol);
}

ConcertTicketing.createColumnVenue = function(concert) {
  var venueCol = this._browseDoc.createElement("td");
  venueCol.className = "venue";

  if (concert.venue != "<generic />") {
    var venueName = concert.venue;
    venueName = unescape(decodeURIComponent(concert.venue));
    var anchor = this._browseDoc.createElement("a");
    this.makeLink(anchor, this.appendPartnerParam(concert.venueURL), "venue");
    var venueColLabel = this._browseDoc.createTextNode(venueName);
    anchor.appendChild(venueColLabel);
    venueCol.appendChild(anchor);
  }

  return (venueCol);
}

/* Generic shortcut for creating a DIV or SPAN & assigning it a style class */
ConcertTicketing.createBlock = function(blockname, makeSpan) {
  var block;
  if (makeSpan)
    var block = this._browseDoc.createElement("span");
  else
    var block = this._browseDoc.createElement("div");
  block.className=blockname;
  return block;
}

/* Methods connected to the groupby menulist & filter checkbox on the chrome */
ConcertTicketing.changeFilter = function(updateCheckbox) {
  this.filterLibraryArtists = !this.filterLibraryArtists;
  Application.prefs.setValue("extensions.concerts.filterLibraryArtists",
      this.filterLibraryArtists);
#ifdef METRICS_ENABLED
  if (this.filterLibraryArtists)
    gMetrics.metricsInc("concerts", "filter.library", "");
  else
    gMetrics.metricsInc("concerts", "filter.all", "");
#endif
  /* checks the filter checkbox (for the path taken when there are no
     results in the user's city and they click the button to see
     all concerts, rather than checking the checkbox themselves */
  if (updateCheckbox) {
    var checkbox = document.getElementById("checkbox-library-artists");
    checkbox.setAttribute("checked", this.filterLibraryArtists);
  }
  songbirdMainWindow.Concerts.updateConcertCount();
  flushDisplay();
  if (this.skSvc.drawingLock) {
    // trigger browseArtists|browseDates to abort
    this.abortDrawing = true;

    // block for release of the lock, and then redraw
    this.blockAndBrowseConcerts(this);
  } else {
    this.browseConcerts(this);
  }
}

ConcertTicketing.groupBy = function(group) {
  Application.prefs.setValue("extensions.concerts.groupby", group);
  
  if (this.skSvc.drawingLock) {
    // trigger browseArtists|browseDates to abort
    this.abortDrawing = true;

    // block for release of the lock, and then redraw
    this.blockAndBrowseConcerts(this);
  } else {
    this.browseConcerts(this);
  }
}

/* Spins until drawingLock is released, and then triggers a browseConcerts() */
ConcertTicketing.blockAndBrowseConcerts = function blockAndBrowseConcerts(ct) {
  if (Cc["@songbirdnest.com/Songbird/Concerts/Songkick;1"]
    .getService(Ci.sbISongkick).drawingLock)
  {
    setTimeout(function() { blockAndBrowseConcerts(ct);}, 100);
  } else {
    ct.browseConcerts(ct);
  }
}
