/*
 *=BEGIN SONGBIRD GPL
 *
 * This file is part of the Songbird web player.
 *
 * Copyright(c) 2005-2010 POTI, Inc.
 * http://www.songbirdnest.com
 *
 * This file may be licensed under the terms of of the
 * GNU General Public License Version 2 (the ``GPL'').
 *
 * Software distributed under the License is distributed
 * on an ``AS IS'' basis, WITHOUT WARRANTY OF ANY KIND, either
 * express or implied. See the GPL for the specific language
 * governing rights and limitations.
 *
 * You should have received a copy of the GPL along with this
 * program. If not, go to http://www.gnu.org/licenses/gpl.html
 * or write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *=END SONGBIRD GPL
 */

#include "sbLocalDatabaseLibraryLoader.h"

#include <nsICategoryManager.h>
#include <nsIAppStartup.h>
#include <nsIFile.h>
#include <nsIGenericFactory.h>
#include <nsILocalFile.h>
#include <nsIObserverService.h>
#include <nsIIOService.h>
#include <nsIPrefBranch.h>
#include <nsIPrefService.h>
#include <nsIPromptService.h>
#include <nsIProperties.h>
#include <nsIPropertyBag2.h>
#include <nsIStringBundle.h>
#include <nsISupportsPrimitives.h>
#include <nsIURI.h>
#include <nsIURL.h>
#include <sbILibrary.h>
#include <sbIMediaList.h>
#ifdef METRICS_ENABLED
#include <sbIMetrics.h>
#endif

#include <nsAutoPtr.h>
#include <nsComponentManagerUtils.h>
#include <nsServiceManagerUtils.h>
#include <nsMemory.h>
#include <nsNetUtil.h>
#include <nsServiceManagerUtils.h>
#include <nsTHashtable.h>
#include <nsXPCOMCID.h>
#include <nsXPFEComponentsCID.h>
#include <prlog.h>
#include <sbLibraryManager.h>
#include <sbMemoryUtils.h>
#include <sbDebugUtils.h>
#include "sbLocalDatabaseCID.h"
#include "sbLocalDatabaseLibraryFactory.h"

#include <DatabaseQuery.h>
#include <sbIPropertyManager.h>
#include <sbPropertiesCID.h>
#include <sbStandardProperties.h>
#include <sbStringBundle.h>

#define PROPERTY_KEY_DATABASEFILE "databaseFile"

/**
 * To log this module, set the following environment variable:
 *   NSPR_LOG_MODULES=sbLocalDatabaseLibraryLoader:5
 */

#define NS_APPSTARTUP_CATEGORY         "app-startup"
#define NS_FINAL_UI_STARTUP_CATEGORY   "final-ui-startup"

#define PREFBRANCH_LOADER SB_PREFBRANCH_LIBRARY "loader."

// These are on PREFBRANCH_LOADER.[index]
#define PREF_DATABASE_GUID     "databaseGUID"
#define PREF_DATABASE_LOCATION "databaseLocation"
#define PREF_LOAD_AT_STARTUP   "loadAtStartup"
#define PREF_RESOURCE_GUID     "resourceGUID"

// This is the pref for the URL on inaccessible library
#define PREF_SUPPORT_INACCESSIBLE_LIBRARY "songbird.url.support.inaccessiblelibrary"

#define MINIMUM_LIBRARY_COUNT 2
#define LOADERINFO_VALUE_COUNT 4

// XXXAus: If you change these, you must change them in DatabaseEngine.cpp
//         as well. Failure to do so will break corrupt database recovery!
#define DBENGINE_GUID_MAIN_LIBRARY      "main@library.songbirdnest.com"
#define DBENGINE_GUID_WEB_LIBRARY       "web@library.songbirdnest.com"
#define DBENGINE_GUID_PLAYQUEUE_LIBRARY "playqueue@library.songbirdnest.com"

// XXXben These should be renamed and standardized somehow.
#define SB_NAMEKEY_MAIN_LIBRARY                            \
  "&chrome://songbird/locale/songbird.properties#servicesource.library"
#define SB_NAMEKEY_WEB_LIBRARY                             \
  "&chrome://songbird/locale/songbird.properties#device.weblibrary"
#define SB_NAMEKEY_PLAYQUEUE_LIBRARY                       \
  "&chrome://songbird/locale/songbird.properties#playqueue.library"

#define SB_CUSTOMTYPE_MAIN_LIBRARY                            \
  "local"
#define SB_CUSTOMTYPE_WEB_LIBRARY                             \
  "web"
#define SB_CUSTOMTYPE_PLAYQUEUE_LIBRARY                       \
  "playqueue"

#define DEFAULT_COLUMNSPEC_WEB_LIBRARY \
  NS_LL("http://songbirdnest.com/data/1.0#trackName 264 ") \
  NS_LL("http://songbirdnest.com/data/1.0#duration 56 ") \
  NS_LL("http://songbirdnest.com/data/1.0#artistName 209 ") \
  NS_LL("http://songbirdnest.com/data/1.0#originPageImage 44 ") \
  NS_LL("http://songbirdnest.com/data/1.0#created 119 d ") \
  NS_LL("http://songbirdnest.com/data/1.0#downloadButton 83")


NS_IMPL_ISUPPORTS2(sbLocalDatabaseLibraryLoader, sbILibraryLoader, nsIObserver)
sbLocalDatabaseLibraryLoader::sbLocalDatabaseLibraryLoader()
: m_DetectedCorruptLibrary(PR_FALSE)
, m_DeleteLibrariesAtShutdown(PR_FALSE)
{
  SB_PRLOG_SETUP(sbLocalDatabaseLibraryLoader);

  TRACE("sbLocalDatabaseLibraryLoader[0x%x] - Created", this);
}

sbLocalDatabaseLibraryLoader::~sbLocalDatabaseLibraryLoader()
{
  TRACE("sbLocalDatabaseLibraryLoader[0x%x] - Destroyed", this);
}

/**
 * \brief
 */
nsresult
sbLocalDatabaseLibraryLoader::Init()
{
  TRACE("sbLocalDatabaseLibraryLoader[0x%x] - Init", this);

  nsresult rv;

  nsCOMPtr<nsIObserverService> observerService =
    do_GetService("@mozilla.org/observer-service;1", &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = observerService->AddObserver(this, NS_FINAL_UI_STARTUP_CATEGORY,
                                      PR_FALSE);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = observerService->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID,
                                      PR_FALSE);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCOMPtr<nsIPrefService> prefService =
    do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  mRootBranch = do_QueryInterface(prefService, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 libraryKeysCount;
  char** libraryKeys;

  rv = mRootBranch->GetChildList(PREFBRANCH_LOADER, &libraryKeysCount,
                                 &libraryKeys);
  NS_ENSURE_SUCCESS(rv, rv);

  sbAutoFreeXPCOMArray<char**> autoFree(libraryKeysCount, libraryKeys);

  PRBool success =
    mLibraryInfoTable.Init(PR_MAX(MINIMUM_LIBRARY_COUNT,
                                  libraryKeysCount / LOADERINFO_VALUE_COUNT));
  NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

  for (PRUint32 index = 0; index < libraryKeysCount; index++) {
    // Should be something like "songbird.library.loader.2.loadAtStartup".
    nsCAutoString pref(libraryKeys[index]);
    NS_ASSERTION(StringBeginsWith(pref, NS_LITERAL_CSTRING(PREFBRANCH_LOADER)),
                 "Bad pref string!");

    PRUint32 branchLength = NS_LITERAL_CSTRING(PREFBRANCH_LOADER).Length();

    PRInt32 firstDotIndex = pref.FindChar('.', branchLength);
    NS_ASSERTION(firstDotIndex != -1, "Bad pref string!");

    PRUint32 keyLength = firstDotIndex - branchLength;
    NS_ASSERTION(keyLength > 0, "Bad pref string!");

    // Should be something like "1".
    nsCAutoString keyString(Substring(pref, branchLength, keyLength));
    PRUint32 libraryKey = keyString.ToInteger(&rv);
    NS_ENSURE_SUCCESS(rv, rv);

    // Should be something like "songbird.library.loader.13.".
    nsCAutoString branchString(Substring(pref, 0, branchLength + keyLength + 1));
    NS_ASSERTION(StringEndsWith(branchString, NS_LITERAL_CSTRING(".")),
                 "Bad pref string!");

    if (!mLibraryInfoTable.Get(libraryKey, nsnull)) {
      nsAutoPtr<sbLibraryLoaderInfo> newLibraryInfo(new sbLibraryLoaderInfo());
      NS_ENSURE_TRUE(newLibraryInfo, NS_ERROR_OUT_OF_MEMORY);

      rv = newLibraryInfo->Init(branchString);
      NS_ENSURE_SUCCESS(rv, rv);

      success = mLibraryInfoTable.Put(libraryKey, newLibraryInfo);
      NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

      newLibraryInfo.forget();
    }
  }

  mLibraryInfoTable.Enumerate(VerifyEntriesCallback, nsnull);

  return NS_OK;
}

nsresult
sbLocalDatabaseLibraryLoader::EnsureDefaultLibraries()
{
  PRBool databasesOkay = PR_TRUE;
  nsresult retval = NS_OK;
  
  nsresult rv =
    EnsureDefaultLibrary(NS_LITERAL_CSTRING(SB_PREF_MAIN_LIBRARY),
                         NS_LITERAL_STRING(DBENGINE_GUID_MAIN_LIBRARY),
                         NS_LITERAL_STRING(SB_NAMEKEY_MAIN_LIBRARY),
                         NS_LITERAL_STRING(SB_CUSTOMTYPE_MAIN_LIBRARY),
                         EmptyString());
  if (NS_FAILED(rv)) {
    databasesOkay = PR_FALSE;
    retval = rv;
  }

  rv = EnsureDefaultLibrary(NS_LITERAL_CSTRING(SB_PREF_WEB_LIBRARY),
                            NS_LITERAL_STRING(DBENGINE_GUID_WEB_LIBRARY),
                            NS_LITERAL_STRING(SB_NAMEKEY_WEB_LIBRARY),
                            NS_LITERAL_STRING(SB_CUSTOMTYPE_WEB_LIBRARY),
                            NS_MULTILINE_LITERAL_STRING(DEFAULT_COLUMNSPEC_WEB_LIBRARY));
  if (NS_FAILED(rv)) {
    databasesOkay = PR_FALSE;
    retval = rv;
  }

  rv = EnsureDefaultLibrary(NS_LITERAL_CSTRING(SB_PREF_PLAYQUEUE_LIBRARY),
                            NS_LITERAL_STRING(DBENGINE_GUID_PLAYQUEUE_LIBRARY),
                            NS_LITERAL_STRING(SB_NAMEKEY_PLAYQUEUE_LIBRARY),
                            NS_LITERAL_STRING(SB_CUSTOMTYPE_PLAYQUEUE_LIBRARY),
                            EmptyString());
  if (NS_FAILED(rv)) {
    databasesOkay = PR_FALSE;
    retval = rv;
  }

  if (! databasesOkay) {
    // Bad database problem.  Later we'll prompt the user.  Now is so early
    // that they would see the prompt again after a silent restart, like
    // the ones that happen on firstrun - component registration, etc.
    // (see Observe() for the prompt call.)
    m_DetectedCorruptLibrary = PR_TRUE;

#ifdef METRICS_ENABLED
    // metric: corrupt database at startup
    nsCOMPtr<sbIMetrics> metrics =
      do_CreateInstance("@songbirdnest.com/Songbird/Metrics;1", &rv);
    NS_ASSERTION(NS_SUCCEEDED(rv), "Failed to get metrics service");
    if (NS_SUCCEEDED(rv)) {
      nsString metricsCategory = NS_LITERAL_STRING("app");
      nsString metricsId = NS_LITERAL_STRING("library.error");
      rv = metrics->MetricsInc(metricsCategory, metricsId, EmptyString());
      NS_ASSERTION(NS_SUCCEEDED(rv), "Failed to post metric");
    }
#endif // METRICS_ENABLED

  }

  return retval;
}

nsresult
sbLocalDatabaseLibraryLoader::EnsureDefaultLibrary(const nsACString& aLibraryGUIDPref,
                                                   const nsAString& aDefaultDatabaseGUID,
                                                   const nsAString& aLibraryNameKey,
                                                   const nsAString& aCustomType,
                                                   const nsAString& aDefaultColumnSpec)
{
  nsCAutoString resourceGUIDPrefKey(aLibraryGUIDPref);

  // Figure out the GUID for this library.
  nsAutoString resourceGUID;
  PRInt32 libraryInfoIndex = -1;

  // The prefs here should point to a library resourceGUID.
  nsCOMPtr<nsISupportsString> supportsString;
  nsresult rv = mRootBranch->GetComplexValue(resourceGUIDPrefKey.get(),
                                             NS_GET_IID(nsISupportsString),
                                             getter_AddRefs(supportsString));
  if (NS_SUCCEEDED(rv)) {
    // Use the value stored in the prefs.
    rv = supportsString->GetData(resourceGUID);
    NS_ENSURE_SUCCESS(rv, rv);

    NS_ASSERTION(!resourceGUID.IsEmpty(), "Should have a resource GUID here!");

    // See if this library already exists in the hashtable.
    sbLibraryExistsInfo existsInfo(resourceGUID);
    mLibraryInfoTable.EnumerateRead(LibraryExistsCallback, &existsInfo);
    
    libraryInfoIndex = existsInfo.index;
  }

  sbLibraryLoaderInfo* libraryInfo;
  if ((libraryInfoIndex == -1) ||
      (!mLibraryInfoTable.Get(libraryInfoIndex, &libraryInfo))) {
    // The library wasn't in our hashtable, so make a new sbLibraryLoaderInfo
    // object. That will take care of setting the prefs up correctly.
    PRUint32 index = GetNextLibraryIndex();

    nsCAutoString prefKey(PREFBRANCH_LOADER);
    prefKey.AppendInt(index);
    prefKey.AppendLiteral(".");

    nsAutoPtr<sbLibraryLoaderInfo>
      newLibraryInfo(CreateDefaultLibraryInfo(prefKey, aDefaultDatabaseGUID,
                                              nsnull, aLibraryNameKey));
    if (!newLibraryInfo || !mLibraryInfoTable.Put(index, newLibraryInfo)) {
      return NS_ERROR_FAILURE;
    }

    // Set the resource GUID into the prefs.
    newLibraryInfo->GetResourceGUID(resourceGUID);
    NS_ENSURE_FALSE(resourceGUID.IsEmpty(), NS_ERROR_UNEXPECTED);

    supportsString = do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = supportsString->SetData(resourceGUID);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mRootBranch->SetComplexValue(resourceGUIDPrefKey.get(),
                                      NS_GET_IID(nsISupportsString),
                                      supportsString);
    NS_ENSURE_SUCCESS(rv, rv);

    libraryInfo = newLibraryInfo.forget();
  }

#ifdef DEBUG
  nsAutoString guid;
  libraryInfo->GetDatabaseGUID(guid);
  NS_ASSERTION(!guid.IsEmpty(), "Empty GUID!");
#endif

  // Make sure this library loads at startup.
  if (!libraryInfo->GetLoadAtStartup()) {
    rv = libraryInfo->SetLoadAtStartup(PR_TRUE);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // And make sure that the database file actually exists and is accessible.
  nsCOMPtr<nsILocalFile> location = libraryInfo->GetDatabaseLocation();
  NS_ENSURE_TRUE(location, NS_ERROR_UNEXPECTED);

  nsCOMPtr<sbILibraryFactory> localDatabaseLibraryFactory =
    do_GetService(SB_LOCALDATABASE_LIBRARYFACTORY_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  sbLocalDatabaseLibraryFactory *libraryFactory = 
    reinterpret_cast<sbLocalDatabaseLibraryFactory *>(localDatabaseLibraryFactory.get());

  nsCOMPtr<sbILibrary> library;
  rv = libraryFactory->CreateLibraryFromDatabase(location,
                                                 getter_AddRefs(library),
                                                 nsnull,
                                                 resourceGUID);
  if (NS_FAILED(rv)) {
    // We can't access this required database file. For now we're going to
    // simply make a new blank database in the default location and switch 
    // the preferences to use it.
    location = libraryFactory->GetFileForGUID(aDefaultDatabaseGUID);
    NS_ENSURE_TRUE(location, NS_ERROR_FAILURE);

    // Make sure we can access this one.
    rv = libraryFactory->CreateLibraryFromDatabase(location,
                                                   getter_AddRefs(library));
    NS_ENSURE_SUCCESS(rv, rv);

    // And update the libraryInfo object.
    rv = libraryInfo->SetDatabaseGUID(aDefaultDatabaseGUID);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = libraryInfo->SetDatabaseLocation(location);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = library->GetGuid(resourceGUID);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = libraryInfo->SetResourceGUID(resourceGUID);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Set the name.
  rv = library->SetName(aLibraryNameKey);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = library->SetProperty(NS_LITERAL_STRING(SB_PROPERTY_CUSTOMTYPE), 
                            aCustomType);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = library->SetProperty(NS_LITERAL_STRING(SB_PROPERTY_ISSORTABLE), 
                            NS_LITERAL_STRING("1"));
  NS_ENSURE_SUCCESS(rv, rv);

  if (!aDefaultColumnSpec.IsEmpty()) {
    rv = library->SetProperty(NS_LITERAL_STRING(SB_PROPERTY_DEFAULTCOLUMNSPEC),
                              aDefaultColumnSpec);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

/**
 * \brief
 */
sbLibraryLoaderInfo*
sbLocalDatabaseLibraryLoader::CreateDefaultLibraryInfo(const nsACString& aPrefKey,
                                                       const nsAString& aDatabaseGUID,
                                                       nsILocalFile* aDatabaseFile,
                                                       const nsAString& aLibraryNameKey)
{
  nsAutoPtr<sbLibraryLoaderInfo> newLibraryInfo(new sbLibraryLoaderInfo());
  NS_ENSURE_TRUE(newLibraryInfo, nsnull);

  nsresult rv = newLibraryInfo->Init(aPrefKey);
  NS_ENSURE_SUCCESS(rv, nsnull);

  nsCOMPtr<sbILibraryFactory> localDatabaseLibraryFactory =
    do_GetService(SB_LOCALDATABASE_LIBRARYFACTORY_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, nsnull);

  sbLocalDatabaseLibraryFactory *libraryFactory = 
    reinterpret_cast<sbLocalDatabaseLibraryFactory *>(localDatabaseLibraryFactory.get());
  
  nsAutoString databaseGUID;

  if (!aDatabaseGUID.IsEmpty()) {
    databaseGUID.Assign(aDatabaseGUID);
  }
  else {
    NS_ASSERTION(aDatabaseFile, "You must supply either the GUID or file!");

    // Figure out the GUID from the filename.
    libraryFactory->GetGUIDFromFile(aDatabaseFile, databaseGUID);
    NS_ENSURE_FALSE(databaseGUID.IsEmpty(), nsnull);
  }

  rv = newLibraryInfo->SetDatabaseGUID(databaseGUID);
  NS_ENSURE_SUCCESS(rv, nsnull);

  nsCOMPtr<nsILocalFile> location;

  if (aDatabaseFile) {
    location = aDatabaseFile;
  }
  else {
    NS_ASSERTION(!aDatabaseGUID.IsEmpty(),
                 "You must specify either the GUID or file!");

    // Figure out the file from the GUID.
    location = libraryFactory->GetFileForGUID(aDatabaseGUID);
    NS_ENSURE_TRUE(location, nsnull);
  }

  rv = newLibraryInfo->SetDatabaseLocation(location);
  NS_ENSURE_SUCCESS(rv, nsnull);

  rv = newLibraryInfo->SetLoadAtStartup(PR_TRUE);
  NS_ENSURE_SUCCESS(rv, nsnull);

  // The resource GUID is unknown at this point. Load the library and get it.
  nsCOMPtr<sbILibrary> library;
  rv = libraryFactory->CreateLibraryFromDatabase(location,
                                                 getter_AddRefs(library));
  NS_ENSURE_SUCCESS(rv, nsnull);

  if (!aLibraryNameKey.IsEmpty()) {
    nsCOMPtr<sbIMediaList> mediaList = do_QueryInterface(library, &rv);
    NS_ENSURE_SUCCESS(rv, nsnull);

    // Set the name.
    rv = mediaList->SetName(aLibraryNameKey);
    NS_ENSURE_SUCCESS(rv, nsnull);
  }

  nsAutoString resourceGUID;
  rv = library->GetGuid(resourceGUID);
  NS_ENSURE_SUCCESS(rv, nsnull);

  rv = newLibraryInfo->SetResourceGUID(resourceGUID);
  NS_ENSURE_SUCCESS(rv, nsnull);

  nsCOMPtr<nsIPrefService> prefService = do_QueryInterface(mRootBranch, &rv);
  NS_ENSURE_SUCCESS(rv, nsnull);

  rv = prefService->SavePrefFile(nsnull);
  NS_ENSURE_SUCCESS(rv, nsnull);

  return newLibraryInfo.forget();
}


/*
 * Something bad has happened to the user's database(s).  Prompt to
 * tell them that we need to delete their libraries.  If the user says
 * OK, set a flag and shut down the app; the actual deletion is done
 * in the Observe() method at shutdown time.
 */
NS_METHOD
sbLocalDatabaseLibraryLoader::PromptToDeleteLibraries() 
{
  nsresult rv;

  nsCOMPtr<nsIPromptService> promptService =
    do_GetService("@mozilla.org/embedcomp/prompt-service;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 buttons = nsIPromptService::BUTTON_POS_0 * nsIPromptService::BUTTON_TITLE_IS_STRING + 
                     nsIPromptService::BUTTON_POS_1 * nsIPromptService::BUTTON_TITLE_IS_STRING + 
                     nsIPromptService::BUTTON_POS_1_DEFAULT;

  PRInt32 promptResult;

  // get dialog strings
  sbStringBundle bundle;

  nsAutoString dialogTitle = bundle.Get("corruptdatabase.dialog.title");

  nsAutoString dialogText = bundle.Get("corruptdatabase.dialog.text");

  nsAutoString deleteText = bundle.Get("corruptdatabase.dialog.buttons.delete");

  nsAutoString continueText =
                 bundle.Get("corruptdatabase.dialog.buttons.cancel");


  // prompt.
  rv = promptService->ConfirmEx(nsnull,
                                dialogTitle.BeginReading(),
                                dialogText.BeginReading(),
                                buttons,            
                                deleteText.BeginReading(),   // button 0
                                continueText.BeginReading(), // button 1
                                nsnull,                      // button 2
                                nsnull,                      // no checkbox
                                nsnull,                      // no check value
                                &promptResult);     
  NS_ENSURE_SUCCESS(rv, rv);

  // "Delete" means delete & restart.  "Continue" means let the app
  // start anyway.
  if (promptResult == 0) { 
    m_DeleteLibrariesAtShutdown = PR_TRUE;

#ifdef METRICS_ENABLED
    // metric: user chose to delete corrupt library
    nsCOMPtr<sbIMetrics> metrics =
     do_CreateInstance("@songbirdnest.com/Songbird/Metrics;1", &rv);
    NS_ASSERTION(NS_SUCCEEDED(rv), "Failed to get metrics service");

    // Metrics may not be available.
    if (metrics) {
      nsString metricsCategory = NS_LITERAL_STRING("app");
      nsString metricsId = NS_LITERAL_STRING("library.error.reset");
      rv = metrics->MetricsInc(metricsCategory, metricsId, EmptyString());
      NS_ASSERTION(NS_SUCCEEDED(rv), "Failed to post metric");
    }
#endif // METRICS_ENABLED

    // now attempt to quit/restart.
    nsCOMPtr<nsIAppStartup> appStartup = 
     (do_GetService(NS_APPSTARTUP_CONTRACTID, &rv));
    NS_ENSURE_SUCCESS(rv, rv);

    appStartup->Quit(nsIAppStartup::eForceQuit | nsIAppStartup::eRestart); 
  }

  return NS_OK;
}

NS_METHOD
sbLocalDatabaseLibraryLoader::PromptInaccessibleLibraries() 
{
  nsresult rv;

  nsCOMPtr<nsIPromptService> promptService =
    do_GetService("@mozilla.org/embedcomp/prompt-service;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 buttons = nsIPromptService::BUTTON_POS_0 * nsIPromptService::BUTTON_TITLE_IS_STRING + 
                     nsIPromptService::BUTTON_POS_0_DEFAULT;

  PRInt32 promptResult;

  // get dialog strings
  sbStringBundle bundle;

  nsTArray<nsString> params;
  nsCOMPtr<nsIProperties> dirService =
    do_GetService("@mozilla.org/file/directory_service;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  
  nsCOMPtr<nsIFile> profileDir;
  rv = dirService->Get("ProfD",
                       NS_GET_IID(nsIFile),
                       getter_AddRefs(profileDir));
  NS_ENSURE_SUCCESS(rv, rv);
  
  nsString profilePath;
  rv = profileDir->GetPath(profilePath);
  NS_ENSURE_SUCCESS(rv, rv);

  params.AppendElement(profilePath);
  
  nsCOMPtr<nsIPrefBranch> prefBranch =
    do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  
  nsString url;
  char* urlBuffer = nsnull;
  rv = prefBranch->GetCharPref(PREF_SUPPORT_INACCESSIBLE_LIBRARY, &urlBuffer);
  if (NS_SUCCEEDED(rv)) {
    url.Assign(NS_ConvertUTF8toUTF16(nsCString(urlBuffer)));
    NS_Free(urlBuffer);
  } else {
    #if PR_LOGGING
      nsresult __rv = rv;
      NS_ENSURE_SUCCESS_BODY(rv, rv);
    #endif /* PR_LOGGING */
    url = bundle.Get("database.inaccessible.dialog.url", "<error>");
  }
  params.AppendElement(url);

  nsAutoString dialogTitle = bundle.Get("database.inaccessible.dialog.title");
  nsAutoString dialogText = bundle.Format("database.inaccessible.dialog.text",
                                          params);
  nsAutoString buttonText = bundle.Get("database.inaccessible.dialog.buttons.quit");

  // prompt.
  rv = promptService->ConfirmEx(nsnull,
                                dialogTitle.BeginReading(),
                                dialogText.BeginReading(),
                                buttons,            
                                buttonText.BeginReading(),   // button 0
                                nsnull,                      // button 1
                                nsnull,                      // button 2
                                nsnull,                      // no checkbox
                                nsnull,                      // no check value
                                &promptResult);     
  NS_ENSURE_SUCCESS(rv, rv);

  // now attempt to quit/restart.
  nsCOMPtr<nsIAppStartup> appStartup = 
    (do_GetService(NS_APPSTARTUP_CONTRACTID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  appStartup->Quit(nsIAppStartup::eForceQuit); 

  return NS_OK;
}

/* static */ void
sbLocalDatabaseLibraryLoader::RemovePrefBranch(const nsACString& aPrefBranch)
{
  nsresult rv;
  nsCOMPtr<nsIPrefService> prefService =
    do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv,);

  nsCAutoString prefBranch(aPrefBranch);

  nsCOMPtr<nsIPrefBranch> doomedBranch;
  rv = prefService->GetBranch(prefBranch.get(), getter_AddRefs(doomedBranch));
  NS_ENSURE_SUCCESS(rv,);

  rv = doomedBranch->DeleteBranch("");
  NS_ENSURE_SUCCESS(rv,);

  rv = prefService->SavePrefFile(nsnull);
  NS_ENSURE_SUCCESS(rv,);
}

PRUint32
sbLocalDatabaseLibraryLoader::GetNextLibraryIndex()
{
  for (PRUint32 index = 0; ; index++) {
    if (!mLibraryInfoTable.Get(index, nsnull)) {
      return index;
    }
  }
  NS_NOTREACHED("Shouldn't be here!");
  return 0;
}

/* static */ PLDHashOperator PR_CALLBACK
sbLocalDatabaseLibraryLoader::LoadLibrariesCallback(nsUint32HashKey::KeyType aKey,
                                                    sbLibraryLoaderInfo* aEntry,
                                                    void* aUserData)
{
  sbLoaderInfo* loaderInfo = static_cast<sbLoaderInfo*>(aUserData);
  NS_ASSERTION(loaderInfo, "Doh, how did this happen?!");

  if (!aEntry->GetLoadAtStartup()) {
    return PL_DHASH_NEXT;
  }

  nsCOMPtr<nsILocalFile> databaseFile = aEntry->GetDatabaseLocation();
  NS_ASSERTION(databaseFile, "Must have a file here!");

  nsCOMPtr<sbILibrary> library;
  nsresult rv =
    loaderInfo->libraryFactory->CreateLibraryFromDatabase(databaseFile,
                                                          getter_AddRefs(library));
  NS_ENSURE_SUCCESS(rv, PL_DHASH_NEXT);
  
  rv = loaderInfo->libraryManager->RegisterLibrary(library, PR_TRUE);
  NS_ENSURE_SUCCESS(rv, PL_DHASH_NEXT);

  return PL_DHASH_NEXT;
}

/* static */ PLDHashOperator PR_CALLBACK
sbLocalDatabaseLibraryLoader::LibraryExistsCallback(nsUint32HashKey::KeyType aKey,
                                                    sbLibraryLoaderInfo* aEntry,
                                                    void* aUserData)
{
  sbLibraryExistsInfo* existsInfo =
    static_cast<sbLibraryExistsInfo*>(aUserData);
  NS_ASSERTION(existsInfo, "Doh, how did this happen?!");

  nsAutoString resourceGUID;
  aEntry->GetResourceGUID(resourceGUID);
  NS_ASSERTION(!resourceGUID.IsEmpty(), "GUID can't be empty here!");

  if (resourceGUID.Equals(existsInfo->resourceGUID)) {
    existsInfo->index = (PRInt32)aKey;
    return PL_DHASH_STOP;
  }

  return PL_DHASH_NEXT;
}

/* static */ PLDHashOperator PR_CALLBACK
sbLocalDatabaseLibraryLoader::VerifyEntriesCallback(nsUint32HashKey::KeyType aKey,
                                                    nsAutoPtr<sbLibraryLoaderInfo>& aEntry,
                                                    void* aUserData)
{
  nsCAutoString prefBranch;
  aEntry->GetPrefBranch(prefBranch);
  NS_ASSERTION(!prefBranch.IsEmpty(), "This can't be empty!");

  nsAutoString databaseGUID;
  aEntry->GetDatabaseGUID(databaseGUID);
  if (databaseGUID.IsEmpty()) {
    NS_WARNING("One of the libraries was missing a database GUID and will be removed.");
    RemovePrefBranch(prefBranch);
    return PL_DHASH_REMOVE;
  }

  nsAutoString resourceGUID;
  aEntry->GetResourceGUID(resourceGUID);
  if (resourceGUID.IsEmpty()) {
    NS_WARNING("One of the libraries was missing a resource GUID and will be removed.");
    RemovePrefBranch(prefBranch);
    return PL_DHASH_REMOVE;
  }

  nsCOMPtr<nsILocalFile> location = aEntry->GetDatabaseLocation();
  if (!location) {
    NS_WARNING("One of the libraries had no location and will be removed.");
    RemovePrefBranch(prefBranch);
    return PL_DHASH_REMOVE;
  }

  return PL_DHASH_NEXT;
}

/**
 * \brief Load all of the libraries we need for startup.
 */
NS_IMETHODIMP
sbLocalDatabaseLibraryLoader::OnRegisterStartupLibraries(sbILibraryManager* aLibraryManager)
{
  TRACE("sbLocalDatabaseLibraryLoader[0x%x] - LoadLibraries", this);

  nsresult rv = Init();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = EnsureDefaultLibraries();
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbILibraryFactory> localDatabaseLibraryFactory =
    do_GetService(SB_LOCALDATABASE_LIBRARYFACTORY_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  sbLocalDatabaseLibraryFactory *libraryFactory = 
    reinterpret_cast<sbLocalDatabaseLibraryFactory *>(localDatabaseLibraryFactory.get());
  
  sbLoaderInfo info(aLibraryManager, libraryFactory);

  PRUint32 SB_UNUSED_IN_RELEASE(enumeratedLibraries) = 
    mLibraryInfoTable.EnumerateRead(LoadLibrariesCallback, &info);
  NS_ASSERTION(enumeratedLibraries >= MINIMUM_LIBRARY_COUNT, "Too few libraries enumerated!");

  return NS_OK;
}

NS_IMETHODIMP
sbLocalDatabaseLibraryLoader::OnLibraryStartupModified(sbILibrary* aLibrary,
                                                       PRBool aLoadAtStartup)
{
  NS_ENSURE_ARG_POINTER(aLibrary);

  // See if we support this library type.
  nsCOMPtr<sbILibraryFactory> factory;
  nsresult rv = aLibrary->GetFactory(getter_AddRefs(factory));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString factoryType;
  rv = factory->GetType(factoryType);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ENSURE_TRUE(factoryType.EqualsLiteral(SB_LOCALDATABASE_LIBRARYFACTORY_TYPE),
                 NS_ERROR_NOT_AVAILABLE);

  // See if this library already exists in the hashtable.
  nsAutoString resourceGUID;
  rv = aLibrary->GetGuid(resourceGUID);
  NS_ENSURE_SUCCESS(rv, rv);

  sbLibraryExistsInfo existsInfo(resourceGUID);
  mLibraryInfoTable.EnumerateRead(LibraryExistsCallback, &existsInfo);

  sbLibraryLoaderInfo* libraryInfo;
  if ((existsInfo.index == -1) ||
      (!mLibraryInfoTable.Get(existsInfo.index, &libraryInfo))) {

    // The library wasn't in the hashtable so make sure that we can recreate
    // it.
    nsCOMPtr<nsIPropertyBag2> creationParameters;
    rv = aLibrary->GetCreationParameters(getter_AddRefs(creationParameters));
    NS_ENSURE_SUCCESS(rv, rv);

    NS_NAMED_LITERAL_STRING(fileKey, PROPERTY_KEY_DATABASEFILE);
    nsCOMPtr<nsILocalFile> databaseFile;
    rv = creationParameters->GetPropertyAsInterface(fileKey,
                                                    NS_GET_IID(nsILocalFile),
                                                    getter_AddRefs(databaseFile));
    NS_ENSURE_SUCCESS(rv, rv);

    NS_ASSERTION(databaseFile, "This can't be null!");

    // Make a new sbLibraryLoaderInfo object.
    PRUint32 index = GetNextLibraryIndex();

    nsCAutoString prefKey(PREFBRANCH_LOADER);
    prefKey.AppendInt(index);
    prefKey.AppendLiteral(".");

    nsAutoPtr<sbLibraryLoaderInfo>
      newLibraryInfo(CreateDefaultLibraryInfo(prefKey, EmptyString(),
                                              databaseFile));
    if (!newLibraryInfo || !mLibraryInfoTable.Put(index, newLibraryInfo)) {
      return NS_ERROR_FAILURE;
    }

    rv = newLibraryInfo->SetDatabaseLocation(databaseFile);
    NS_ENSURE_SUCCESS(rv, rv);

    libraryInfo = newLibraryInfo.forget();
  }

  rv = libraryInfo->SetLoadAtStartup(aLoadAtStartup);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP 
sbLocalDatabaseLibraryLoader::Observe(nsISupports *aSubject,
                                      const char *aTopic,
                                      const PRUnichar *aData)
{
  nsresult rv;

  if (strcmp(aTopic, NS_FINAL_UI_STARTUP_CATEGORY) == 0) {
    if (m_DetectedCorruptLibrary) {
      /* check to see if the prefs file is writable, to recover from people
       * running the app with sudo :(
       */
      nsCOMPtr<nsIProperties> dirService =
        do_GetService("@mozilla.org/file/directory_service;1", &rv);
      NS_ENSURE_SUCCESS(rv, rv);
      
      nsCOMPtr<nsIFile> prefFile;
      rv = dirService->Get("PrefF",
                           NS_GET_IID(nsIFile),
                           getter_AddRefs(prefFile));
      NS_ENSURE_SUCCESS(rv, rv);
      
      PRBool prefExists;
      /* if the pref file is missing we go through the normal first-run process
       * and therefore don't need to care about this
       */
      PRBool prefWritable = PR_TRUE;
      rv = prefFile->Exists(&prefExists);
      if (NS_SUCCEEDED(rv) && prefExists) {
        rv = prefFile->IsWritable(&prefWritable);
        NS_ENSURE_SUCCESS(rv, rv);
      }

      if (prefWritable) {
        rv = PromptToDeleteLibraries();
        NS_ENSURE_SUCCESS(rv, rv);
      } else {
        // database file is not writable, just bail
        rv = PromptInaccessibleLibraries();
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }
  } else if (strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID) == 0) {
    // By now, databases should all be closed so it is safe to delete
    // the db directory.
    if (m_DeleteLibrariesAtShutdown) {
      // get profile directory
      nsCOMPtr<nsIProperties> directoryService(
        do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID, &rv));
      NS_ENSURE_SUCCESS(rv, rv);
     
      nsCOMPtr<nsIFile> siteDBDir;
      rv = directoryService->Get("ProfD", NS_GET_IID(nsIFile),
                                 getter_AddRefs(siteDBDir));
      NS_ENSURE_SUCCESS(rv, rv);
     
      // append the database dir name
      siteDBDir->Append(NS_LITERAL_STRING("db"));
     

      // Delete all the databases but the metrics DB. (which hopefully
      // is not corrupt)
      nsCOMPtr<nsISimpleEnumerator> dirEnumerator;
      rv = siteDBDir->GetDirectoryEntries(getter_AddRefs(dirEnumerator));
      NS_ENSURE_SUCCESS(rv, rv);

      PRBool hasMore;
      dirEnumerator->HasMoreElements(&hasMore);
      while (hasMore && NS_SUCCEEDED(rv)) {
        nsCOMPtr<nsISupports> aSupport;
        rv = dirEnumerator->GetNext(getter_AddRefs(aSupport));
        if (NS_FAILED(rv)) break;
        nsCOMPtr<nsIFile> curFile(do_QueryInterface(aSupport, &rv));
        if (NS_FAILED(rv)) break;

        nsString leafName;
        rv = curFile->GetLeafName(leafName);
        if (NS_FAILED(rv)) break;

#ifdef METRICS_ENABLED
        if (leafName.Compare(NS_LITERAL_STRING("metrics.db")) != 0) {
          rv = curFile->Remove(false);
          NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "Unable to delete file");
        }
#else
        rv = curFile->Remove(false);
        NS_WARN_IF_FALSE(NS_SUCCEEDED(rv), "Unable to delete file");
#endif
       
        dirEnumerator->HasMoreElements(&hasMore);
      }

      // We want to prompt the user to rescan on restart.
      nsCAutoString scancompleteBranch("songbird.firstrun.scancomplete");
      sbLocalDatabaseLibraryLoader::RemovePrefBranch(scancompleteBranch);

      // And delete all the library prefs, so they get recreated on
      // startup.  (It would be nice to not need to do this, so that
      // users could delete their db directory by hand and restart, but
      // bad things happen due to prefs and it's not worth putting time
      // into.)
      nsCAutoString prefBranch(PREFBRANCH_LOADER);
      sbLocalDatabaseLibraryLoader::RemovePrefBranch(prefBranch);
    }
  }

   return NS_OK;
}



/**
 * sbLibraryLoaderInfo implementation
 */
nsresult
sbLibraryLoaderInfo::Init(const nsACString& aPrefKey)
{
  nsresult rv;
  nsCOMPtr<nsIPrefService> prefService =
    do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString prefBranchString(aPrefKey);
  rv = prefService->GetBranch(prefBranchString.get(),
                              getter_AddRefs(mPrefBranch));
  NS_ENSURE_SUCCESS(rv, rv);

  mDatabaseGUIDKey.Assign(PREF_DATABASE_GUID);
  mLocationKey.Assign(PREF_DATABASE_LOCATION);
  mStartupKey.Assign(PREF_LOAD_AT_STARTUP);
  mResourceGUIDKey.Assign(PREF_RESOURCE_GUID);

  // Now ensure that the key exists.
  PRBool exists;
  rv = mPrefBranch->PrefHasUserValue(mStartupKey.get(), &exists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!exists) {
    rv = SetLoadAtStartup(PR_FALSE);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult
sbLibraryLoaderInfo::SetDatabaseGUID(const nsAString& aGUID)
{
  NS_ENSURE_FALSE(aGUID.IsEmpty(), NS_ERROR_INVALID_ARG);

  nsresult rv;
  nsCOMPtr<nsISupportsString> supportsString =
    do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = supportsString->SetData(aGUID);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPrefBranch->SetComplexValue(mDatabaseGUIDKey.get(),
                                    NS_GET_IID(nsISupportsString),
                                    supportsString);

  return NS_OK;
}

void
sbLibraryLoaderInfo::GetDatabaseGUID(nsAString& _retval)
{
  _retval.Truncate();

  nsCOMPtr<nsISupportsString> supportsString;
  nsresult rv = mPrefBranch->GetComplexValue(mDatabaseGUIDKey.get(),
                                             NS_GET_IID(nsISupportsString),
                                             getter_AddRefs(supportsString));
  NS_ENSURE_SUCCESS(rv,);

  rv = supportsString->GetData(_retval);
  NS_ENSURE_SUCCESS(rv,);
}

nsresult
sbLibraryLoaderInfo::SetDatabaseLocation(nsILocalFile* aLocation)
{
  NS_ENSURE_ARG_POINTER(aLocation);

  nsresult rv;
  nsCOMPtr<nsIFile> file = do_QueryInterface(aLocation, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString filePath;
  rv = file->GetNativePath(filePath);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPrefBranch->SetCharPref(mLocationKey.get(), filePath.get());
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

already_AddRefed<nsILocalFile>
sbLibraryLoaderInfo::GetDatabaseLocation()
{
  nsresult rv;
  nsCOMPtr<nsILocalFile> location = do_CreateInstance(NS_LOCAL_FILE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, nsnull);

  nsCAutoString filePath;
  rv = mPrefBranch->GetCharPref(mLocationKey.get(), getter_Copies(filePath));
  NS_ENSURE_SUCCESS(rv, nsnull);

  rv = location->InitWithNativePath(filePath);
  NS_ENSURE_SUCCESS(rv, nsnull);

  nsILocalFile* _retval;
  NS_ADDREF(_retval = location);
  return _retval;
}

nsresult
sbLibraryLoaderInfo::SetLoadAtStartup(PRBool aLoadAtStartup)
{
  nsresult rv = mPrefBranch->SetBoolPref(mStartupKey.get(), aLoadAtStartup);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

PRBool
sbLibraryLoaderInfo::GetLoadAtStartup()
{
  PRBool loadAtStartup;
  nsresult rv = mPrefBranch->GetBoolPref(mStartupKey.get(), &loadAtStartup);
  NS_ENSURE_SUCCESS(rv, PR_FALSE);

  return loadAtStartup;
}

nsresult
sbLibraryLoaderInfo::SetResourceGUID(const nsAString& aGUID)
{
  NS_ENSURE_FALSE(aGUID.IsEmpty(), NS_ERROR_INVALID_ARG);

  nsresult rv;
  nsCOMPtr<nsISupportsString> supportsString =
    do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = supportsString->SetData(aGUID);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mPrefBranch->SetComplexValue(mResourceGUIDKey.get(),
                                    NS_GET_IID(nsISupportsString),
                                    supportsString);

  return NS_OK;
}

void
sbLibraryLoaderInfo::GetResourceGUID(nsAString& _retval)
{
  _retval.Truncate();

  nsCOMPtr<nsISupportsString> supportsString;
  nsresult rv = mPrefBranch->GetComplexValue(mResourceGUIDKey.get(),
                                             NS_GET_IID(nsISupportsString),
                                             getter_AddRefs(supportsString));
  NS_ENSURE_SUCCESS(rv,);

  rv = supportsString->GetData(_retval);
  NS_ENSURE_SUCCESS(rv,);
}

void
sbLibraryLoaderInfo::GetPrefBranch(nsACString& _retval)
{
  _retval.Truncate();

  nsCAutoString prefBranch;
  nsresult rv = mPrefBranch->GetRoot(getter_Copies(prefBranch));
  NS_ENSURE_SUCCESS(rv,);

  _retval.Assign(prefBranch);
}
