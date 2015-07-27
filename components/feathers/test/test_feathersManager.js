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
 * \brief FeathersManager test file
 */

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

// Default layouts/skin, read in from the preferences.
// Used in |previousSkinName()| and |previousLayoutURL()|.
var gDefaultMainLayoutURL = "";
var gDefaultSecondaryLayoutURL = "";
var gDefaultSkinName = "";
var gBundledSkins = []; // Empty, will get filled in testAddonMetadataReader
var gBundledLayouts = []; // Empty, will get filled in testAddonMetadataReader

// These skins / Layouts are required to be registered
var gRequiredSkinInternalNames = [ "bluemonday" ];
var gRequiredLayoutURLs = [ "chrome://bluemonday/content/xul/mainplayer.xul",
                            "chrome://bluemonday/content/xul/miniplayer.xul" ];

// Preference constants for default layout/skin/feather
// @see sbFeathersManager.js for information about each pref.
const PREF_DEFAULT_MAIN_LAYOUT       = "songbird.feathers.default_main_layout";
const PREF_DEFAULT_SECONDARY_LAYOUT  = "songbird.feathers.default_secondary_layout";
const PREF_DEFAULT_SKIN_INTERNALNAME = "songbird.feathers.default_skin_internalname";

var feathersManager =  Components.classes['@songbirdnest.com/songbird/feathersmanager;1']
                                 .getService(Components.interfaces.sbIFeathersManager);

// Needed to collect all feather-AddOns
Components.utils.import("resource://app/jsmodules/RDFHelper.jsm");


// List of skin descriptions used in the test cases
var skins = [];

// List of layout descriptions used in the test cases
var layouts = [];


// Make dataremotes to tweak feathers settings
var createDataRemote =  new Components.Constructor(
              "@songbirdnest.com/Songbird/DataRemote;1",
              Components.interfaces.sbIDataRemote, "init");

var layoutDataRemote = createDataRemote("feathers.selectedLayout", null);
var skinDataRemote = createDataRemote("selectedSkin", "general.skins.");
var previousLayoutDataRemote = createDataRemote("feathers.previousLayout", null);
var previousSkinDataRemote = createDataRemote("feathers.previousSkin", null);

var originalDataRemoteValues = [];



/**
 * Store the original values of all FeathersManager dataremotes
 * so that we can restore them once the tests are complete
 */
function saveDataRemotes() {
  originalDataRemoteValues.push(layoutDataRemote.stringValue);
  originalDataRemoteValues.push(skinDataRemote.stringValue);
  originalDataRemoteValues.push(previousLayoutDataRemote.stringValue);  
  originalDataRemoteValues.push(previousSkinDataRemote.stringValue);  
}

/**
 * Restore saved FeathersManager dataremote values.
 * This is so that changes made during testing do not
 * end up in your profile.
 */
function restoreDataRemotes() {
  layoutDataRemote.stringValue = originalDataRemoteValues.shift();
  skinDataRemote.stringValue = originalDataRemoteValues.shift();
  previousLayoutDataRemote.stringValue = originalDataRemoteValues.shift(); 
  previousSkinDataRemote.stringValue = originalDataRemoteValues.shift();
}



/**
 * Creates a generic description object that works for skins and layouts
 */
function newFeathersDescription() {
  // NOT a constructor because the feathers manager holds on to things for
  // way too long; by carefully staying out of the scope, we reduce the set of
  // objects being held via the XPCOM interface (in particular, the global)
  var o = new Object();
  o.wrappedJSObject = o;
  o.QueryInterface = XPCOMUtils.generateQI([Ci.sbISkinDescription,
                                            Ci.sbILayoutDescription]);
  return o;
}


/**
 * sbIFeathersChangeListener for testing callback
 * support in FeathersManager
 */
var feathersChangeListener = {

  // Count update notifications so that they can be confirmed
  // with an assert.
  updateCounter: 0,
  onFeathersUpdate: function() {
    this.updateCounter++;
  },
  
  // Set skin and layout to be expected on next select,
  // and reset when received.
  expectLayout: null,
  expectSkin: null,
  onFeathersSelectRequest: function(layoutDesc, skinDesc) {
    assertEqual(layoutDesc.wrappedJSObject, this.expectLayout);
    assertEqual(skinDesc.wrappedJSObject, this.expectSkin);
  },
  onFeathersSelectComplete: function(layoutDesc, skinDesc) {
    assertEqual(layoutDesc.wrappedJSObject, this.expectLayout);
    assertEqual(skinDesc.wrappedJSObject, this.expectSkin);
    this.expectLayout = null;
    this.expectSkin = null;
    testFinished();
  },
  
  QueryInterface: function(iid) {
    if (!iid.equals(Components.interfaces.sbIFeathersChangeListener)) 
      throw Components.results.NS_ERROR_NO_INTERFACE;
    return this;
  }
}


/** 
 * Turn an nsISimpleEnumerator into a generator
 */ 
function wrapEnumerator(enumerator, iface)
{
  while (enumerator.hasMoreElements()) {
    yield enumerator.getNext().QueryInterface(iface);
  }
}


/**
 * Make sure that the given enumerator contains all objects in 
 * the given list and nothing else.
 */
function assertEnumeratorEqualsArray(enumerator, list) {
  list = list.concat([]); // Clone list before modifying 
  for (var item in enumerator) {
    assertTrue(list.indexOf(item.wrappedJSObject) > -1);
    list.splice(list.indexOf(item.wrappedJSObject), 1);
  }
  assertEqual(list.length, 0);
}


/**
 * Make sure that the given enumerator contains objects 
 * with a field matching the given list
 *
 * TODO: Rename to something that makes more sense?
 */
function assertEnumeratorMatchesFieldArray(enumerator, field, list) {
  list = list.concat([]); // Clone list before modifying 
  for (var item in enumerator) {
    assertTrue(list.indexOf(item[field]) > -1,
        item[field] + " not found in list: '" + list + "'\n");
    list.splice(list.indexOf(item[field]), 1);
  }
  assertEqual(list.length, 0);
}



/** 
 * Make sure that the addon metadata reader is populating
 * the feathers manager.
 */
function testAddonMetadataReader()
{
  // Get all AddOns
  var addons = RDFHelper.help(
      "rdf:addon-metadata",
      "urn:songbird:addon:root",
      RDFHelper.DEFAULT_RDF_NAMESPACES
    );
  
  // Go through all AddOns and search for feathers and layouts,
  // push them into gBundledSkins and gBundledLayouts
  var deprecatedLayouts = []; // Will hold Gonzo layouts
  for (var i = 0; i < addons.length; i++) {
    var addon = addons[i];
    if (addon.skin) {
      log("FeatherAddon found: " + addon.id);
      var skins = addon.skin;
      for (var j = 0; j < skins.length; j++){
        log(" > Feather: " + skins[j].name + " (" + skins[j].internalName + ")");
        if (addon.id == "gonzo@songbirdnest.com"){
          log("   ignored, it is the Gonzo feather");
          continue; // Do not include Gonzo, it doesn't provide a feather
        }
        // Push the found feather into the Array of Feathers we will use
        gBundledSkins.push(skins[j].internalName.toString());
      }
      var layouts = addon.layout;
      for (var j = 0; j < layouts.length; j++){
        log(" > Layout: " + layouts[j].name + " (" + layouts[j].url + ")");
        // Push the found layout into the Array of layouts we will use
        // Gonzo gets another array, which will get joined with the other
        // in the end, so a valid feather with two connected layouts are
        // first in list
        if (addon.id == "gonzo@songbirdnest.com") {
          deprecatedLayouts.push(layouts[j].url.toString());
        } else {
          gBundledLayouts.push(layouts[j].url.toString());
        }
      }
    }
  }
  // Add Gonzo layouts to the array, so the length is correct but they can't
  // be first
  gBundledLayouts = gBundledLayouts.concat(deprecatedLayouts);
  
  // Verify all skins added properly
  var skinNames = gBundledSkins;
  
  assertEqual(feathersManager.skinCount, skinNames.length);
  var enumerator = wrapEnumerator(feathersManager.getSkinDescriptions(),
                     Components.interfaces.sbISkinDescription);
  assertEnumeratorMatchesFieldArray(enumerator, "internalName", skinNames);
  
  // Verify all layouts added properly
  var layoutURLs = gBundledLayouts;
  
  assertEqual(feathersManager.layoutCount, layoutURLs.length);
  enumerator = wrapEnumerator(feathersManager.getLayoutDescriptions(), 
                     Components.interfaces.sbILayoutDescription);
  assertEnumeratorMatchesFieldArray(enumerator, "url", layoutURLs);

  // Verify mappings
  enumerator = wrapEnumerator(feathersManager.getSkinsForLayout(layoutURLs[0]), 
                 Components.interfaces.sbISkinDescription);
  assertEnumeratorMatchesFieldArray(enumerator, "internalName", [gBundledSkins[0]]);
  enumerator = wrapEnumerator(feathersManager.getSkinsForLayout(layoutURLs[1]), 
                              Components.interfaces.sbISkinDescription);
  assertEnumeratorMatchesFieldArray(enumerator, "internalName", [gBundledSkins[0]]);
  
  // Verify showChrome
  // Chrome is only enabled on Mac OS X
  var sysInfo = Components.classes["@mozilla.org/system-info;1"]
                  .getService(Components.interfaces.nsIPropertyBag2);
  if (sysInfo.getProperty("name") == "Darwin")
    assertEqual( feathersManager.isChromeEnabled(layoutURLs[0], skinNames[0]), true);
  else
    assertEqual( feathersManager.isChromeEnabled(layoutURLs[0], skinNames[1]), false);

  // Verify onTop
  assertEqual( feathersManager.isOnTop(layoutURLs[0], skinNames[0]), false);
  assertEqual( feathersManager.isOnTop(layoutURLs[1], skinNames[0]), false);
  
  // Verify we have the Coppery skin and the two Coppery layouts
  var thing;
  for each (thing in gRequiredSkinInternalNames)
    assertTrue( feathersManager.getSkinDescription(thing) );
  for each (thing in gRequiredLayoutURLs)
    assertTrue( feathersManager.getLayoutDescription(thing) );
}


/** 
 * Try revert using the default feathers
 */
function testDefaultRevert() {
  // Set feathers dataremotes to some junk value 
  previousLayoutDataRemote.stringValue = "somethingrandom";
  layoutDataRemote.stringValue = "somethingelse";
  
  // Confirm that revert switches us to the primary fallback
  feathersManager.switchFeathers(feathersManager.previousLayoutURL,
                                 feathersManager.previousSkinName);

  testPending();

  assertEqual(skinDataRemote.stringValue, gDefaultSkinName);
  assertEqual(layoutDataRemote.stringValue, gDefaultMainLayoutURL);
  
  // Now revert again, taking us to the secondary fallback
  feathersManager.switchFeathers(feathersManager.previousLayoutURL,
                                 feathersManager.previousSkinName);

  testPending();

  assertEqual(skinDataRemote.stringValue, gDefaultSkinName);
  assertEqual(layoutDataRemote.stringValue, gDefaultSecondaryLayoutURL);
}


/** 
 * Populate the feathers manager
 */
function submitFeathers()
{
  // Make some skins
  var skin = newFeathersDescription();
  skin.name = "Blue Skin";
  skin.internalName = "blue/1.0";
  skins.push(skin);
  feathersManager.registerSkin(skin);

  skin = newFeathersDescription();
  skin.name = "Red Skin";
  skin.internalName = "red/1.0";
  skins.push(skin);
  feathersManager.registerSkin(skin);

  skin = newFeathersDescription();
  skin.name = "Orange Skin";
  skin.internalName = "orange/1.0";
  skins.push(skin);
  feathersManager.registerSkin(skin);
  
  // Make some layouts
  var layout = newFeathersDescription();
  layout.name = "Big Layout";
  layout.url = "chrome://biglayout/content/mainwin.xul";
  layouts.push(layout);
  feathersManager.registerLayout(layout);
  
  layout = newFeathersDescription();
  layout.name = "Mini Layout";
  layout.url = "chrome://minilayout/content/mini.xul";
  layouts.push(layout);
  feathersManager.registerLayout(layout);
  
  // Create some mappings
  // Blue -> big/ontop, Red -> big, Orange -> mini with chrome
  feathersManager.assertCompatibility(layouts[1].url, skins[0].internalName, false, true);
  feathersManager.assertCompatibility(layouts[1].url, skins[1].internalName, false, false);
  feathersManager.assertCompatibility(layouts[0].url, skins[2].internalName, true, false);
}


/**
 * Remove everything from the feathers manager
 */
function teardown() {
  skins.forEach(function(skin) { 
    feathersManager.unregisterSkin(skin);
  });

  layouts.forEach(function(layout) { 
    feathersManager.unregisterLayout(layout);
  });

  // Remove mappings
  // Blue -> big, Red -> big, Orange -> mini 
  feathersManager.unassertCompatibility(layouts[1].url, skins[0].internalName);
  feathersManager.unassertCompatibility(layouts[1].url, skins[1].internalName);
  
  // Remove change listener before final modification to confirm
  // that it actually gets unhooked.
  feathersManager.removeListener(feathersChangeListener);
  
  feathersManager.unassertCompatibility(layouts[0].url, skins[2].internalName);
}





/**
 * Test all functionality in sbFeathersManager
 */
function runTest () {
  
  saveDataRemotes();

  // Read in the default layout and skin from prefs.
  var AppPrefs = Cc["@mozilla.org/fuel/application;1"]
                     .getService(Ci.fuelIApplication).prefs;
  gDefaultMainLayoutURL = AppPrefs.getValue(PREF_DEFAULT_MAIN_LAYOUT, "");
  gDefaultSecondaryLayoutURL = 
    AppPrefs.getValue(PREF_DEFAULT_SECONDARY_LAYOUT, "");
  gDefaultSkinName = AppPrefs.getValue(PREF_DEFAULT_SKIN_INTERNALNAME, "");

  layoutDataRemote.stringValue = "";
  skinDataRemote.stringValue = "";
  previousLayoutDataRemote.stringValue = "";
  previousSkinDataRemote.stringValue = "";
  
  // Register for change callbacks
  feathersManager.addListener(feathersChangeListener);
  
  ///////////////////////////////////////////////////
  // Test skins/layouts registered via extensions  //
  ///////////////////////////////////////////////////

  testAddonMetadataReader();
  
  testDefaultRevert();
 
  // Now remove the extension descriptions in order to 
  // make testing the registration functions a bit easier
  enumerator = wrapEnumerator(feathersManager.getSkinDescriptions(),
                     Components.interfaces.sbISkinDescription);  
  for (var item in enumerator) feathersManager.unregisterSkin(item);
  enumerator = wrapEnumerator(feathersManager.getLayoutDescriptions(), 
                     Components.interfaces.sbILayoutDescription);
  for (var item in enumerator) feathersManager.unregisterLayout(item);
  

  //////////////////////////////////
  // Test registration functions  //
  //////////////////////////////////

  feathersChangeListener.updateCounter = 0;

  submitFeathers();
  
  // ------------------------
  // Test change notification.  We should have seen 8 update notifications
  assertEqual(feathersChangeListener.updateCounter, 8);
  
  // ------------------------
  // Test Getters / Make sure setup succeeded
  
  // Verify all skins added properly
  assertEqual(feathersManager.skinCount, skins.length);
  var enumerator = wrapEnumerator(feathersManager.getSkinDescriptions(),
                     Components.interfaces.sbISkinDescription);
  assertEnumeratorEqualsArray(enumerator, skins);
  
  // Verify all layouts added properly
  assertEqual(feathersManager.layoutCount, layouts.length);
  enumerator = wrapEnumerator(feathersManager.getLayoutDescriptions(), 
                     Components.interfaces.sbILayoutDescription);
  assertEnumeratorEqualsArray(enumerator, layouts);

  // Test description getters
  var desc = feathersManager.getSkinDescription(skins[1].internalName).wrappedJSObject;
  assertEqual(desc, skins[1]);
  desc = feathersManager.getLayoutDescription(layouts[1].url).wrappedJSObject;
  assertEqual(desc, layouts[1]);

  // ------------------------
  // Verify mappings
  enumerator = wrapEnumerator(feathersManager.getSkinsForLayout(layouts[0].url), 
                 Components.interfaces.sbISkinDescription);
  assertEnumeratorEqualsArray(enumerator, [skins[2]]);
  enumerator = wrapEnumerator(feathersManager.getSkinsForLayout(layouts[1].url), 
                 Components.interfaces.sbISkinDescription);
  assertEnumeratorEqualsArray(enumerator, [skins[0], skins[1]]);
  enumerator = wrapEnumerator(feathersManager.getLayoutsForSkin(skins[0].internalName), 
                 Components.interfaces.sbILayoutDescription);
  assertEnumeratorEqualsArray(enumerator, [layouts[1]]);
  
  // ------------------------
  // Verify showChrome
  assertEqual( feathersManager.isChromeEnabled(layouts[1].url, skins[0].internalName), false);
  assertEqual( feathersManager.isChromeEnabled(layouts[1].url, skins[1].internalName), false);
  assertEqual( feathersManager.isChromeEnabled(layouts[0].url, skins[2].internalName), true);

  // ------------------------
  // Verify onTop
  assertEqual( feathersManager.isOnTop(layouts[1].url, skins[0].internalName), false);
  assertEqual( feathersManager.isOnTop(layouts[1].url, skins[1].internalName), false);
  assertEqual( feathersManager.isOnTop(layouts[0].url, skins[2].internalName), false);


  // ------------------------
  // Test feathers select and revert
  
  // Manually set the current feathers so that we can test revert
  layoutDataRemote.stringValue = layouts[0].url;
  skinDataRemote.stringValue = skins[2].internalName;
  
  
  // First with an invalid pair
  var failed = false;
  try {
    feathersManager.switchFeathers(layouts[0].url, skins[1].internalName);
  } catch (e) {
    failed = true;
  }
  assertEqual(failed, true);


  // Then with a valid pair.  Expect an onSelect callback.
  feathersChangeListener.expectSkin = skins[0];
  feathersChangeListener.expectLayout = layouts[1];
  feathersManager.switchFeathers(layouts[1].url, skins[0].internalName);

  testPending();

  // Make sure onSelect callback occurred
  assertEqual(feathersChangeListener.expectSkin, null);
  assertEqual(feathersChangeListener.expectLayout, null);
  
  
  // Now revert, expecting the values manually set above.
  feathersChangeListener.expectSkin = skins[2];
  feathersChangeListener.expectLayout = layouts[0];
  feathersManager.switchFeathers(feathersManager.previousLayoutURL,
                                 feathersManager.previousSkinName);

  testPending();

  // Make sure onSelect callback occurred
  assertEqual(feathersChangeListener.expectSkin, null);
  assertEqual(feathersChangeListener.expectLayout, null);
    

  ////////////////////////////////////
  // Get rid of everything we added //
  ////////////////////////////////////

  // Reset update counter so we can test teardown
  feathersChangeListener.updateCounter = 0;

  teardown();
  
  // ------------------------
  // Test change notification.  We should have seen 7 update notifications,
  // as we unhooked before the last removeMapping call.
  assertEqual(feathersChangeListener.updateCounter, 7);
  feathersChangeListener.updateCounter = 0;
  
  // ------------------------
  // Make sure teardown succeeded
  assertEqual(feathersManager.skinCount, 0);
  enumerator = feathersManager.getSkinDescriptions();
  assertEqual(enumerator.hasMoreElements(), false); 

  assertEqual(feathersManager.layoutCount, 0);
  enumerator = feathersManager.getLayoutDescriptions();
  assertEqual(enumerator.hasMoreElements(), false); 

  // ------------------------  
  // Verify mappings are gone
  enumerator = feathersManager.getSkinsForLayout(layouts[0].url);
  assertEqual(enumerator.hasMoreElements(), false); 
  enumerator = feathersManager.getSkinsForLayout(layouts[1].url);
  assertEqual(enumerator.hasMoreElements(), false); 
  
  restoreDataRemotes();
  
  return Components.results.NS_OK;
}
