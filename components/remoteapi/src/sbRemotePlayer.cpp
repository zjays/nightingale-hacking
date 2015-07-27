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

#include "sbRemotePlayer.h"

#include "sbRemoteAPIUtils.h"
#include "sbRemoteCommands.h"
#include "sbRemoteLibrary.h"
#include "sbRemoteLibraryBase.h"
#include "sbRemoteMediaItemStatusEvent.h"
#include "sbRemotePlaylistClickEvent.h"
#include "sbRemoteSecurityEvent.h"
#include "sbRemoteSiteLibrary.h"
#include "sbRemoteWebLibrary.h"
#include "sbRemoteWebPlaylist.h"
#include "sbSecurityMixin.h"
#include "sbURIChecker.h"
#include <sbClassInfoUtils.h>
#include <sbIDataRemote.h>
#include <sbIDeviceManager.h>
#include <sbIDownloadDevice.h>
#include <sbILibrary.h>
#include <sbIMediacoreEvent.h>
#include <sbIMediacoreEventTarget.h>
#include <sbIMediacoreManager.h>
#include <sbIMediacorePlaybackControl.h>
#include <sbIMediacoreSequencer.h>
#include <sbIMediacoreVolumeControl.h>
#include <sbIMediaList.h>
#include <sbIMediaListView.h>
#ifdef METRICS_ENABLED
#include <sbIMetrics.h>
#endif
#include <sbIRemoteAPIService.h>
#include <sbIPlaylistClickEvent.h>
#include <sbIPlaylistCommands.h>
#include <sbITabBrowser.h>
#include <sbIPropertyInfo.h>
#include <sbIPropertyManager.h>
#include <sbPropertiesCID.h>
#include <sbStandardProperties.h>
#include <sbIPropertyBuilder.h>
#include <sbStringUtils.h>

#include <nsAutoPtr.h>
#include <nsDOMJSUtils.h>
#include <nsIArray.h>
#include <nsICategoryManager.h>
#include <nsIContentViewer.h>
#include <nsIDocShell.h>
#include <nsIDocShellTreeItem.h>
#include <nsIDocShellTreeOwner.h>
#include <nsIDocument.h>
#include <nsIDOMDocument.h>
#include <nsIDOMDocumentEvent.h>
#include <nsIDOMElement.h>
#include <nsIDOMEvent.h>
#include <nsIDOMEventTarget.h>
#include <nsIDOMMouseEvent.h>
#include <nsIDOMNodeList.h>
#include <nsIDOMNSEvent.h>
#include <nsIDOMWindow.h>
#include <nsPIDOMWindow.h>
#include <nsIDOMWindowInternal.h>
#include <nsIDOMXULDocument.h>
#include <nsIDOMXULElement.h>
#include <nsIInterfaceRequestorUtils.h>
#include <nsIJSContextStack.h>
#include <nsIPrefBranch.h>
#include <nsIPresShell.h>
#include <nsIPrivateDOMEvent.h>
#include <nsIPromptService.h>
#include <nsIScriptGlobalObject.h>
#include <nsIScriptNameSpaceManager.h>
#include <nsIStringBundle.h>
#include <nsITreeSelection.h>
#include <nsITreeView.h>
#include <nsIURI.h>
#include <nsIVariant.h>
#include <nsIWindowMediator.h>
#include <nsMemory.h>
#include <nsNetUtil.h>
#include <nsServiceManagerUtils.h>
#include <nsStringGlue.h>
#include <prlog.h>

// added for download support. Some will stay after bug 3521 is fixed
#include <sbIDeviceBase.h>
#include <sbIDeviceManager.h>
#include <sbILibraryManager.h>
#include <nsIDialogParamBlock.h>
#include <nsISupportsPrimitives.h>
#include <nsIWindowWatcher.h>

/*
 * To log this module, set the following environment variable:
 *   NSPR_LOG_MODULES=sbRemotePlayer:5
 */
#ifdef PR_LOGGING
static PRLogModuleInfo* gRemotePlayerLog = nsnull;
#endif

#undef LOG
#undef TRACE
#define LOG(args) PR_LOG(gRemotePlayerLog, PR_LOG_WARN, args)
#define TRACE(args) PR_LOG(gRemotePlayerLog, PR_LOG_DEBUG, args)
#ifdef __GNUC__
#define __FUNCTION__ __PRETTY_FUNCTION__
#endif

static NS_DEFINE_CID(kRemotePlayerCID, SONGBIRD_REMOTEPLAYER_CID);

const static char* sPublicWProperties[] =
  { "playback_control:position" };

const static char* sPublicRProperties[] =
  { "site:apiVersionMajor",
    "site:apiVersionMinor",
    "site:playing",
    "site:paused",
    "site:repeat",
    "site:shuffle",
    "site:position",
    "site:duration",
    "site:volume",
    "site:mute",
    "site:name",
    "library_read:playlists",
    "site:webLibrary",
    "site:mainLibrary",
    "site:siteLibrary",
    "site:webPlaylist",
    "site:downloadMediaList",
    "playback_read:currentArtist",
    "playback_read:currentAlbum",
    "playback_read:currentTrack",
    "site:commands",
    "classinfo:classDescription",
    "classinfo:contractID",
    "classinfo:classID",
    "classinfo:implementationLanguage",
    "classinfo:flags" };

const static char* sPublicMethods[] =
  { "playback_control:play",
    "playback_control:playMediaList",
    "playback_control:stop",
    "playback_control:pause",
    "playback_control:previous",
    "playback_control:next",
    "playback_control:playURL",
    "library_write:downloadItem",
    "library_write:downloadList",
    "library_write:downloadSelected",
    "site:setSiteScope",
    "library_read:libraries",
    "playback_read:removeListener",
    "playback_read:addListener",
    "site:supportsVersion",
    "site:createTextProperty",
    "site:createDateTimeProperty",
    "site:createURIProperty",
    "site:createNumberProperty",
    "site:createImageProperty",
    "site:createRatingsProperty",
    "site:createButtonProperty",
    "site:createDownloadButtonProperty",
    "site:hasAccess" };

// dataremotes keys that can be listened to
// when you change this, please update /documentation/ListenerTopics.txt thanks!
const static char* sPublicMetadata[] =
  { "metadata.artist",
    "metadata.title",
    "metadata.album",
    "metadata.genre",
    "metadata.position",
    "metadata.length",
    "metadata.position.str",
    "metadata.length.str",
    "playlist.shuffle",
    "playlist.repeat",
    "playlist.shuffle.disabled",
    "faceplate.volume",
    "faceplate.mute",
    "faceplate.playing",
    "faceplate.paused" };

// This is a lookup table for converting the human readable catagory names
// into our internal names (see sbSecurityMinin.cpp:sScopes).
// The first entry is the javascript text, and the second one is for internal,
// we add a : at the end of the internal one because that is what is compared.
const static char* sPublicCategoryConversions[][2] =
  { { "Control Playback", "playback_control:" },
    { "Read Current", "playback_read:" },
    { "Read Library", "library_read:" },
    { "Modify Library", "library_write:" } };

// needs to be in nsEventDispatcher.cpp
#define RAPI_EVENT_CLASS                  NS_LITERAL_STRING("Events")
#define RAPI_EVENT_TYPE                   NS_LITERAL_STRING("remoteapi")
#define RAPI_EVENT_TYPE_DOWNLOADSTART     NS_LITERAL_STRING("downloadstart")
#define RAPI_EVENT_TYPE_DOWNLOADCOMPLETE  NS_LITERAL_STRING("downloadcomplete")
#define RAPI_EVENT_TYPE_BEFORETRACKCHANGE NS_LITERAL_STRING("beforetrackchange")
#define RAPI_EVENT_TYPE_TRACKCHANGE       NS_LITERAL_STRING("trackchange")
#define RAPI_EVENT_TYPE_TRACKINDEXCHANGE  NS_LITERAL_STRING("trackindexchange")
#define RAPI_EVENT_TYPE_BEFOREVIEW        NS_LITERAL_STRING("beforeviewchange")
#define RAPI_EVENT_TYPE_VIEW              NS_LITERAL_STRING("viewchange")
#define RAPI_EVENT_TYPE_STOP              NS_LITERAL_STRING("playbackstopped")
#define SB_PREFS_ROOT                     NS_LITERAL_STRING("songbird.")
#define SB_EVENT_CMNDS_UP                 NS_LITERAL_STRING("playlist-commands-updated")
#define SB_WEB_TABBROWSER                 NS_LITERAL_STRING("sb-tabbrowser")

#define SB_LIB_NAME_MAIN      "main"
#define SB_LIB_NAME_WEB       "web"

#define SB_DATAREMOTE_FACEPLATE_STATUS NS_LITERAL_STRING("faceplate.status.override.text")

#define RAPI_VERSION_MAJOR 1
#define RAPI_VERSION_MINOR 0

// callback for destructor to clear out the observer hashtable
PR_STATIC_CALLBACK(PLDHashOperator)
UnbindAndRelease ( const nsAString &aKey,
                   sbRemoteObserver &aRemObs,
                   void *userArg )
{
  LOG(("UnbindAndRelease(%s, %x %x)", PromiseFlatString(aKey).get(), aRemObs.observer.get(), aRemObs.remote.get()));
  NS_ASSERTION(&aRemObs, "GAH! Hashtable contains a null entry");
  NS_ASSERTION(aRemObs.remote, "GAH! Container contains a null remote");
  NS_ASSERTION(aRemObs.observer, "GAH! Container contains a null observer");

  aRemObs.remote->Unbind();
  return PL_DHASH_REMOVE;
}

// helper class to push a system pricipal so that all calls across xpconnect
// will succeed.  Similar to nsAutoLock, except for principals
class sbAutoPrincipalPusher
{
  public:
    sbAutoPrincipalPusher()
    {
      mStack = do_GetService("@mozilla.org/js/xpc/ContextStack;1");
      if (mStack) {
        nsresult rv = mStack->Push(nsnull);
        if (NS_FAILED(rv)) {
          // failed to push the stack, don't accidentally pop it later
          mStack = nsnull;
        }
      }
    }
    ~sbAutoPrincipalPusher()
    {
      if (mStack) {
        JSContext *cx;
        mStack->Pop(&cx);
      }
    }
    // tell people if we succeeded in pushing the principal
    operator PRBool() const
    {
      return mStack ? PR_TRUE : PR_FALSE ;
    }
  protected:
    nsCOMPtr<nsIJSContextStack> mStack;
};

class sbRemotePlayerEnumCallback : public sbIMediaListEnumerationListener
{
  public:
    NS_DECL_ISUPPORTS

    sbRemotePlayerEnumCallback( nsCOMArray<sbIMediaItem>& aArray ) :
      mArray(aArray) { }

    NS_IMETHODIMP OnEnumerationBegin( sbIMediaList*, PRUint16* _retval )
    {
      NS_ENSURE_ARG(_retval);
      *_retval = sbIMediaListEnumerationListener::CONTINUE;
      return NS_OK;
    }
    NS_IMETHODIMP OnEnumerationEnd( sbIMediaList*, nsresult )
    {
      return NS_OK;
    }
    NS_IMETHODIMP OnEnumeratedItem( sbIMediaList*, sbIMediaItem* aItem, PRUint16* _retval )
    {
      NS_ENSURE_ARG(_retval);
      *_retval = sbIMediaListEnumerationListener::CONTINUE;

      mArray.AppendObject( aItem );

      return NS_OK;
    }
  private:
    nsCOMArray<sbIMediaItem>& mArray;
};
NS_IMPL_ISUPPORTS1( sbRemotePlayerEnumCallback, sbIMediaListEnumerationListener )

NS_IMPL_ISUPPORTS7( sbRemotePlayer,
                    nsIClassInfo,
                    nsISecurityCheckedComponent,
                    sbIRemotePlayer,
                    nsIDOMEventListener,
                    nsISupportsWeakReference,
                    sbIMediacoreEventListener,
                    sbISecurityAggregator )

NS_IMPL_CI_INTERFACE_GETTER6( sbRemotePlayer,
                              nsISecurityCheckedComponent,
                              sbIRemotePlayer,
                              nsIDOMEventListener,
                              nsISupportsWeakReference,
                              sbIMediacoreEventListener,
                              sbISecurityAggregator )

SB_IMPL_CLASSINFO( sbRemotePlayer,
                   SONGBIRD_REMOTEPLAYER_CONTRACTID,
                   SONGBIRD_REMOTEPLAYER_CLASSNAME,
                   nsIProgrammingLanguage::CPLUSPLUS,
                   0,
                   kRemotePlayerCID )

sbRemotePlayer::sbRemotePlayer() :
  mInitialized(PR_FALSE),
  mPrivileged(PR_FALSE)
{
#ifdef PR_LOGGING
  if (!gRemotePlayerLog) {
    gRemotePlayerLog = PR_NewLogModule("sbRemotePlayer");
  }
#endif
  LOG(("sbRemotePlayer::sbRemotePlayer()"));
}

sbRemotePlayer::~sbRemotePlayer()
{
  LOG(("sbRemotePlayer::~sbRemotePlayer()"));

  if (mRemObsHash.IsInitialized()) {
    mRemObsHash.Enumerate(UnbindAndRelease, nsnull);
    mRemObsHash.Clear();
  }

  if (mDownloadCallback)
    mDownloadCallback->Finalize();
  if (mNotificationMgr)
    mNotificationMgr->Cancel();
}

nsresult
sbRemotePlayer::Init()
{
  LOG(("sbRemotePlayer::Init()"));

  nsresult rv;

  // Don't set any scope information here -- wait for either the client to
  // set it or set it when a site library is requested.
  mScopeDomain.SetIsVoid(PR_TRUE);
  mScopePath.SetIsVoid(PR_TRUE);
  mSiteScopeURL.SetIsVoid(PR_TRUE);

  // pull the dom window from the js stack and context
  nsCOMPtr<nsPIDOMWindow> privWindow = sbRemotePlayer::GetWindowFromJS();
  NS_ENSURE_TRUE(privWindow, NS_ERROR_UNEXPECTED);

  mPrivileged = PR_FALSE;

  rv = InitInternal(privWindow);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbRemotePlayer::InitPrivileged(nsIURI* aCodebase, nsIDOMWindow* aWindow)
{
  LOG(("sbRemotePlayer::InitPrivileged()"));

  NS_ASSERTION(aCodebase, "aCodebase is null");
  NS_ASSERTION(aWindow, "aWindow is null");

  nsresult rv;

  rv = sbURIChecker::CheckURI(mScopeDomain, mScopePath, aCodebase);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCString spec;
  rv = aCodebase->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);

  mSiteScopeURL = NS_ConvertUTF8toUTF16(spec);

  nsCOMPtr<nsPIDOMWindow> privWindow = do_QueryInterface(aWindow, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  mPrivileged = PR_TRUE;

  rv = InitInternal(privWindow);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbRemotePlayer::InitInternal(nsPIDOMWindow* aWindow)
{
  LOG(("sbRemotePlayer::InitInternal()"));

  NS_ASSERTION(aWindow, "aWindow is null");

  mPrivWindow = aWindow;

  nsresult rv;
  nsCOMPtr<nsISupportsWeakReference> weakRef = 
    do_GetService(SB_MEDIACOREMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = weakRef->GetWeakReference(getter_AddRefs(mMM));
  NS_ENSURE_SUCCESS(rv, rv);

  mIOService = do_GetService("@mozilla.org/network/io-service;1", &rv);
  NS_ENSURE_SUCCESS( rv, rv );

  PRBool success = mRemObsHash.Init();
  NS_ENSURE_TRUE( success, NS_ERROR_FAILURE );

  success = mCachedLibraries.Init(2);
  NS_ENSURE_TRUE( success, NS_ERROR_FAILURE );

  nsRefPtr<sbSecurityMixin> mixin = new sbSecurityMixin();
  NS_ENSURE_TRUE( mixin, NS_ERROR_OUT_OF_MEMORY );

  // Get the list of IIDs to pass to the security mixin
  nsIID ** iids;
  PRUint32 iidCount;
  GetInterfaces(&iidCount, &iids);

  // initialize our mixin with approved interfaces, methods, properties
  rv = mixin->Init( (sbISecurityAggregator*)this,
                    (const nsIID**)iids, iidCount,
                    sPublicMethods,NS_ARRAY_LENGTH(sPublicMethods),
                    sPublicRProperties,NS_ARRAY_LENGTH(sPublicRProperties),
                    sPublicWProperties, NS_ARRAY_LENGTH(sPublicWProperties),
                    mPrivileged );
  NS_ENSURE_SUCCESS( rv, rv );
  NS_FREE_XPCOM_ALLOCATED_POINTER_ARRAY(iidCount, iids);

  mSecurityMixin = do_QueryInterface(
                          NS_ISUPPORTS_CAST( sbISecurityMixin*, mixin ), &rv );
  NS_ENSURE_SUCCESS( rv, rv );

  //
  // Get the Content Document
  //
  mPrivWindow->GetDocument( getter_AddRefs(mContentDoc) );
  NS_ENSURE_STATE(mContentDoc);

  //
  // Set the content document on our mixin so that it knows where to send
  // notification events
  //
  rv = mixin->SetNotificationDocument( mContentDoc );
  NS_ENSURE_SUCCESS( rv, rv );

  //
  // Get the Chrome Document by going up the docshell tree
  //
  nsIDocShell *docShell = mPrivWindow->GetDocShell();
  NS_ENSURE_STATE(docShell);
  
  nsCOMPtr<nsIDocShellTreeItem> treeItem = do_QueryInterface(docShell, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDocShellTreeItem> rootItem;
  rv = treeItem->GetRootTreeItem(getter_AddRefs(rootItem));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDocShell> rootShell = do_QueryInterface(rootItem, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIContentViewer> contentViewer;
  rv = rootShell->GetContentViewer(getter_AddRefs(contentViewer));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = contentViewer->GetDOMDocument(getter_AddRefs(mChromeDoc));
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_STATE(mChromeDoc);

  LOG(("sbRemotePlayer::Init() -- registering unload listener"));
  // Have our HandleEvent called on page unloads, so we can unhook commands
  nsCOMPtr<nsIDOMEventTarget> eventTarget( do_QueryInterface(mChromeDoc) );
  NS_ENSURE_STATE(eventTarget);
  eventTarget->AddEventListener( NS_LITERAL_STRING("unload"), this , PR_TRUE );

  LOG(("sbRemotePlayer::Init() -- registering PlaylistCellClick listener"));
  eventTarget->AddEventListener( NS_LITERAL_STRING("PlaylistCellClick"), this , PR_TRUE );

  LOG(("sbRemotePlayer::Init() -- registering RemoteAPIPermissionDenied listener"));
  eventTarget->AddEventListener( SB_EVENT_RAPI_PERMISSION_DENIED, this , PR_TRUE );

  LOG(("sbRemotePlayer::Init() -- registering RemoteAPIPermissionChanged listener"));
  eventTarget->AddEventListener( SB_EVENT_RAPI_PERMISSION_CHANGED, this , PR_TRUE );

  mNotificationMgr = new sbRemoteNotificationManager();
  NS_ENSURE_TRUE(mNotificationMgr, NS_ERROR_OUT_OF_MEMORY);

  rv = mNotificationMgr->Init();
  NS_ENSURE_SUCCESS(rv, rv);

  // Set up listener for playback service
  LOG(("sbRemotePlayer::Init() -- registering playback listener"));
  nsCOMPtr<sbIMediacoreEventTarget> target = 
    do_QueryReferent(mMM, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = target->AddListener(this);
  NS_ENSURE_SUCCESS(rv, rv);

  // Set up download callbacks
  mDownloadCallback = new sbRemotePlayerDownloadCallback();
  NS_ENSURE_TRUE(mDownloadCallback, NS_ERROR_OUT_OF_MEMORY);
  rv = mDownloadCallback->Initialize(this);
  NS_ENSURE_SUCCESS(rv, rv);

#ifdef METRICS_ENABLED
  // Set up metrics and count this session
  mMetrics = do_CreateInstance("@songbirdnest.com/Songbird/Metrics;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Count this session
  rv = mMetrics->MetricsInc(NS_LITERAL_STRING("rapi.sessionStarted"),
                            EmptyString(),
                            EmptyString());
  NS_ENSURE_SUCCESS(rv, rv);
#endif

  mInitialized = PR_TRUE;

  return NS_OK;
}

// ---------------------------------------------------------------------------
//
//                          sbISecurityAggregator
//
// ---------------------------------------------------------------------------

NS_IMETHODIMP
sbRemotePlayer::GetRemotePlayer(sbIRemotePlayer * *aRemotePlayer)
{
  NS_ENSURE_ARG_POINTER(aRemotePlayer);

  nsresult rv;
  *aRemotePlayer = nsnull;

  nsCOMPtr<sbIRemotePlayer> remotePlayer;

  rv = QueryInterface( NS_GET_IID( sbIRemotePlayer ), getter_AddRefs( remotePlayer ) );
  NS_ENSURE_SUCCESS( rv, rv );

  remotePlayer.swap( *aRemotePlayer );

  return NS_OK;
}

// ---------------------------------------------------------------------------
//
//                        sbIRemotePlayer
//
// ---------------------------------------------------------------------------

NS_IMETHODIMP
sbRemotePlayer::SupportsVersion( const nsAString &aAPIVersion, 
                                 PRBool *aSupportsVersion )
{
  NS_ENSURE_ARG_POINTER(aSupportsVersion);
  NS_ENSURE_TRUE(!aAPIVersion.IsEmpty(), NS_ERROR_INVALID_ARG);

  *aSupportsVersion = PR_FALSE;

  nsTArray<nsString> substrings;
  nsString_Split(aAPIVersion, NS_LITERAL_STRING("."), substrings);

  nsresult rv = NS_ERROR_UNEXPECTED;
  PRInt32 majorVersion = substrings[0].ToInteger(&rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // No use keeping on comparing if the major version passed in is greater
  // the the current major version.
  if(majorVersion > static_cast<PRInt32>(RAPI_VERSION_MAJOR)) {
    return NS_OK;
  }

  // Make sure we have a minor version. We only use array index 0 and 1, all
  // other strings we get from the split are ignored as our RAPI versioning
  // is always in MAJOR.MINOR format.
  if(substrings.Length() > 1) {
    PRInt32 minorVersion = substrings[1].ToInteger(&rv);
    NS_ENSURE_SUCCESS(rv, rv);

    if(minorVersion <= static_cast<PRInt32>(RAPI_VERSION_MINOR)) {
      *aSupportsVersion = PR_TRUE;
    }
  }
  else {
    // User only passed in a major version and it matched the major version
    // we currently have. 
    *aSupportsVersion = PR_TRUE;
  }

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::GetApiVersionMajor( PRUint32 *aApiVersionMajor )
{
  NS_ENSURE_ARG_POINTER(aApiVersionMajor);

  *aApiVersionMajor = RAPI_VERSION_MAJOR;

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::GetApiVersionMinor( PRUint32 *aApiVersionMinor )
{
  NS_ENSURE_ARG_POINTER(aApiVersionMinor);

  *aApiVersionMinor = RAPI_VERSION_MINOR;

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::GetName( nsAString &aName )
{
  LOG(("sbRemotePlayer::GetName()"));
  aName.AssignLiteral("Songbird");
  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::GetMainLibrary( sbIRemoteLibrary **aMainLibrary )
{
  return Libraries( NS_LITERAL_STRING(SB_LIB_NAME_MAIN), aMainLibrary );
}

NS_IMETHODIMP
sbRemotePlayer::GetWebLibrary( sbIRemoteLibrary **aWebLibrary )
{
  return Libraries( NS_LITERAL_STRING(SB_LIB_NAME_WEB), aWebLibrary );
}

NS_IMETHODIMP
sbRemotePlayer::Libraries( const nsAString &aLibraryID,
                           sbIRemoteLibrary **aLibrary )
{
  NS_ENSURE_ARG_POINTER(aLibrary);
  LOG(( "sbRemotePlayer::Libraries(%s)",
        NS_LossyConvertUTF16toASCII(aLibraryID).get() ));
  nsresult rv;

  // Go ahead and return the cached copy if we have it. This Get call will take
  // care of AddRef'ing for us.
  if ( mCachedLibraries.Get( aLibraryID, aLibrary ) ) {
    return NS_OK;
  }

  // for a newly created library
  nsRefPtr<sbRemoteLibrary> library;

  // NOTE: if you add special library names here, document them in sbIRemotePlayer.idl
  if ( aLibraryID.EqualsLiteral(SB_LIB_NAME_MAIN) ) {
    // No cached main library create a new one.
    LOG(("sbRemotePlayer::Libraries() - creating main library"));
    library = new sbRemoteLibrary(this);
    NS_ENSURE_TRUE( library, NS_ERROR_OUT_OF_MEMORY );
  }
  else if ( aLibraryID.EqualsLiteral(SB_LIB_NAME_WEB) ) {
    // No cached web library create a new one.
    LOG(("sbRemotePlayer::Libraries() - creating web library"));
    library = new sbRemoteWebLibrary(this);
    NS_ENSURE_TRUE( library, NS_ERROR_OUT_OF_MEMORY );
  }
  else {
    // we don't handle anything but "main" and "web" yet.
    return NS_ERROR_INVALID_ARG;
  }

  rv = library->Init();
  NS_ENSURE_SUCCESS( rv, rv );

  rv = library->ConnectToDefaultLibrary( aLibraryID );
  NS_ENSURE_SUCCESS( rv, rv );

  nsCOMPtr<sbIRemoteLibrary> remoteLibrary =
    do_QueryInterface( NS_ISUPPORTS_CAST( sbIRemoteLibrary*, library ), &rv );
  NS_ENSURE_SUCCESS( rv, rv );

  // Now that initialization and library connection has succeeded cache
  // the created library in the hashtable.
  if ( !mCachedLibraries.Put( aLibraryID, remoteLibrary ) ) {
    NS_WARNING("Failed to cache remoteLibrary!");
  }

  NS_ADDREF( *aLibrary = remoteLibrary );
  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::SetSiteScope(const nsACString & aDomain, const nsACString & aPath)
{
  nsresult rv;

#if SB_DEBUG_RAPI
  NS_WARNING("sbRemotePlayer::SetSiteScope() - skipping multiple site library checks");
#else
  NS_ENSURE_TRUE( mScopeDomain.IsVoid(), NS_ERROR_ALREADY_INITIALIZED );
  NS_ENSURE_TRUE( mScopePath.IsVoid(), NS_ERROR_ALREADY_INITIALIZED );
#endif /* SB_DEBUG_RAPI */

  nsCString domain(aDomain);
  nsCString path(aPath);
  nsCOMPtr<nsIURI> codebaseURI;
  nsCOMPtr<sbISecurityMixin> mixin = do_QueryInterface( mSecurityMixin, &rv );
  NS_ENSURE_SUCCESS( rv, rv );
  rv = mixin->GetCodebase( getter_AddRefs(codebaseURI) );
  NS_ENSURE_SUCCESS( rv, rv );

  rv = sbURIChecker::CheckURI(domain, path, codebaseURI);
  NS_ENSURE_SUCCESS( rv, rv );

  mScopeDomain = domain;
  mScopePath = path;

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::GetSiteScope(nsIURI * *aURI)
{
  NS_ENSURE_ARG_POINTER(aURI);

  nsCOMPtr<nsIURI> uri = GetSiteScopeURI();
  uri.swap(*aURI);

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::GetSiteLibrary(sbIRemoteLibrary * *aSiteLibrary)
{
  LOG(( "sbRemotePlayer::GetSiteLibrary(%s)", mScopePath.BeginReading() ));

  nsresult rv;

  // check if the site scope has beens set before; if not, set it implicitly
  if ( mScopeDomain.IsVoid() || mScopePath.IsVoid() ) {
    SetSiteScope( mScopeDomain, mScopePath );
  }

  nsString siteLibraryFilename;
  rv = sbRemoteSiteLibrary::GetFilenameForSiteLibrary( mScopeDomain,
                                                       mScopePath,
                                                       siteLibraryFilename );
  NS_ENSURE_SUCCESS( rv, rv );

  if ( mCachedLibraries.Get( siteLibraryFilename, aSiteLibrary ) ) {
    return NS_OK;
  }

  nsRefPtr<sbRemoteSiteLibrary> library;
  library = new sbRemoteSiteLibrary(this);
  NS_ENSURE_TRUE( library, NS_ERROR_OUT_OF_MEMORY );

  rv = library->Init();
  NS_ENSURE_SUCCESS( rv, rv );

  rv = library->ConnectToSiteLibrary( mScopeDomain, mScopePath );
  NS_ENSURE_SUCCESS( rv, rv );

  nsCOMPtr<sbIRemoteLibrary> remoteLibrary(
    do_QueryInterface( NS_ISUPPORTS_CAST( sbIRemoteSiteLibrary*, library ),
                       &rv ) );
  NS_ENSURE_SUCCESS( rv, rv );

  PRBool success = mCachedLibraries.Put( siteLibraryFilename, remoteLibrary );
  NS_ENSURE_TRUE( success, NS_ERROR_FAILURE );

  NS_ADDREF( *aSiteLibrary = remoteLibrary );
  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::GetCommands( sbIRemoteCommands **aCommandsObject )
{
  NS_ENSURE_ARG_POINTER(aCommandsObject);

  LOG(("sbRemotePlayer::GetCommands()"));

  nsresult rv;
  if (!mCommandsObject) {
    LOG(("sbRemotePlayer::GetCommands() -- creating it"));
    mCommandsObject = new sbRemoteCommands(this);
    NS_ENSURE_TRUE( mCommandsObject, NS_ERROR_OUT_OF_MEMORY );

    rv = mCommandsObject->Init();
    NS_ENSURE_SUCCESS( rv, rv );

    mCommandsObject->SetOwner(this);
    RegisterCommands(PR_TRUE);
  }
  NS_ADDREF( *aCommandsObject = mCommandsObject );
  return NS_OK;
}

nsresult
sbRemotePlayer::RegisterCommands( PRBool aUseDefaultCommands )
{
  NS_ENSURE_STATE(mCommandsObject);
  nsresult rv;

  // store the default command usage
  mUseDefaultCommands = aUseDefaultCommands;

  // Get the PlaylistCommandsManager object and register commands with it.
  nsCOMPtr<sbIPlaylistCommandsManager> mgr(
         do_GetService( "@songbirdnest.com/Songbird/PlaylistCommandsManager;1", &rv ) );
  NS_ENSURE_SUCCESS( rv, rv );

  nsCOMPtr<sbIPlaylistCommands> commands = (sbIPlaylistCommands*) mCommandsObject;
  NS_ENSURE_TRUE( commands, NS_ERROR_UNEXPECTED );

  // XXXredfive - make this pull the GUID from the web playlist
  //              need sbIRemoteMediaLists online before we can do that.
  // Registration of commands is changing soon, for now type='library' works
  NS_ENSURE_SUCCESS( rv, rv );
  rv = mgr->RegisterPlaylistCommandsMediaItem( NS_LITERAL_STRING("remote-test-guid"),
                                               NS_LITERAL_STRING("library"),
                                               commands );
  NS_ASSERTION( NS_SUCCEEDED(rv),
                "Failed to register commands in playlistcommandsmanager" );
  rv = mgr->RegisterPlaylistCommandsMediaItem( NS_LITERAL_STRING("remote-test-guid"),
                                               NS_LITERAL_STRING("simple"),
                                               commands );
  NS_ASSERTION( NS_SUCCEEDED(rv),
                "Failed to register commands in playlistcommandsmanager" );

  OnCommandsChanged();

  return rv;
}

NS_IMETHODIMP
sbRemotePlayer::OnCommandsChanged()
{
  LOG(("sbRemotePlayer::OnCommandsChanged()"));
  if (!mRemWebPlaylist) {
    nsresult rv = InitRemoteWebPlaylist();
    NS_ENSURE_SUCCESS( rv, rv );
  }

  //
  // This is where we want to add code to register the default commands, when
  // the api for that comes in to being. Key off of mUseDefaultCommands.
  //

  // When the commands system is able to broadcast change notices about
  // registered commands this can go away. In the meantime we need to tell
  // the playlist to rescan so it picks up new/deleted commands.
  // Theoretically we could just fire an event here, but it wasn't getting
  // caught in the binding, need to look in to that more.
  mRemWebPlaylist->RescanCommands();
  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::GetWebPlaylist( sbIRemoteWebPlaylist **aWebPlaylist )
{
  LOG(("sbRemotePlayer::GetWebPlaylist()"));
  NS_ENSURE_ARG_POINTER(aWebPlaylist);
  nsresult rv;

  if (!mRemWebPlaylist) {
    rv = InitRemoteWebPlaylist();
    NS_ENSURE_SUCCESS( rv, rv );
  }

  nsCOMPtr<sbIRemoteWebPlaylist> remotePlaylist( do_QueryInterface(
    NS_ISUPPORTS_CAST( sbIRemoteWebPlaylist*, mRemWebPlaylist ), &rv ) );
  NS_ENSURE_SUCCESS( rv, rv );

  NS_ADDREF( *aWebPlaylist = remotePlaylist );
  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::GetDownloadMediaList( sbIRemoteMediaList **aDownloadMediaList )
{
  LOG(("sbRemotePlayer::GetDownloadMediaList()"));
  NS_ENSURE_ARG_POINTER(aDownloadMediaList);

  nsresult rv;
  nsCOMPtr<sbIDownloadDeviceHelper> dh(
    do_GetService( "@songbirdnest.com/Songbird/DownloadDeviceHelper;1", &rv ));
  NS_ENSURE_SUCCESS( rv, rv );

  nsCOMPtr<sbIMediaList> downloadMediaList;
  rv = dh->GetDownloadMediaList(getter_AddRefs(downloadMediaList));
  NS_ENSURE_SUCCESS( rv, rv );

  rv = SB_WrapMediaList( this, downloadMediaList, aDownloadMediaList );
  NS_ENSURE_SUCCESS( rv, rv );

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::DownloadItem( sbIMediaItem *aItem )
{
  LOG(("sbRemotePlayer::DownloadItem()"));

  NS_ENSURE_ARG_POINTER(aItem);

  nsresult rv;

  // Make sure the item is JUST an item, no lists or libraries allowed
  nsCOMPtr<sbIMediaList> listcheck (do_QueryInterface(aItem));
  NS_ENSURE_FALSE( listcheck, NS_ERROR_INVALID_ARG );

  nsCOMPtr<sbIMediaItem> item;

  // The media item must be unwrapped before handing off to the download helper!
  nsCOMPtr<sbIWrappedMediaItem> wrappedItem(do_QueryInterface( aItem, &rv ));
  if (NS_SUCCEEDED(rv)) {
    item = wrappedItem->GetMediaItem();
    NS_ASSERTION( item, "GetMediaItem returned null!" );
  }
  else {
    item = aItem;
  }

  nsCOMPtr<sbIDownloadDeviceHelper> dh(
    do_GetService( "@songbirdnest.com/Songbird/DownloadDeviceHelper;1", &rv ));
  NS_ENSURE_SUCCESS( rv, rv );

  // no check here, download device helper doesn't return values.
  dh->DownloadItem(item);

  mNotificationMgr->Action( sbRemoteNotificationManager::eDownload, nsnull );

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::DownloadList( sbIRemoteMediaList *aList )
{
  LOG(("sbRemotePlayer::DownloadList()"));

  NS_ENSURE_ARG_POINTER(aList);

  nsCOMPtr<sbIMediaList> list;

  // The media list must be unwrapped before handing off to the download helper!
  nsresult rv;
  nsCOMPtr<sbIWrappedMediaList> wrappedList(do_QueryInterface( aList, &rv ));
  if (NS_SUCCEEDED(rv)) {
    list = wrappedList->GetMediaList();
    NS_ASSERTION( list, "GetMediaList returned null!" );
  }
  else {
    list = do_QueryInterface( aList, &rv );
    NS_ENSURE_SUCCESS( rv, rv );
  }

  nsCOMPtr<sbIDownloadDeviceHelper> dh(
    do_GetService( "@songbirdnest.com/Songbird/DownloadDeviceHelper;1", &rv ));
  NS_ENSURE_SUCCESS( rv, rv );

  // no check here, download device helper doesn't return values.
  dh->DownloadAll(list);

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::DownloadSelected( sbIRemoteWebPlaylist *aWebPlaylist )
{
  LOG(("sbRemotePlayer::DownloadSelected()"));

  NS_ENSURE_ARG_POINTER(aWebPlaylist);

  nsCOMPtr<nsISimpleEnumerator> selection;
  nsresult rv = aWebPlaylist->GetSelection( getter_AddRefs(selection) );
  NS_ENSURE_SUCCESS( rv, rv );

  // The media items must be unwrapped before handing off to the download
  // helper!
  nsRefPtr<sbUnwrappingSimpleEnumerator> wrapper(
                                 new sbUnwrappingSimpleEnumerator(selection) );
  NS_ENSURE_TRUE( wrapper, NS_ERROR_OUT_OF_MEMORY );

  nsCOMPtr<sbIDownloadDeviceHelper> dh =
    do_GetService( "@songbirdnest.com/Songbird/DownloadDeviceHelper;1", &rv );
  NS_ENSURE_SUCCESS( rv, rv );

  // no check here, download device helper doesn't return values.
  dh->DownloadSome(wrapper);

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::GetCurrentArtist( nsAString &aCurrentArtist )
{
  LOG(("sbRemotePlayer::GetCurrentArtist()"));
  if (!mdrCurrentArtist) {
    nsresult rv;
    mdrCurrentArtist =
           do_CreateInstance( "@songbirdnest.com/Songbird/DataRemote;1", &rv );
    NS_ENSURE_SUCCESS( rv, rv );
    rv = mdrCurrentArtist->Init( NS_LITERAL_STRING("metadata.artist"),
                                 SB_PREFS_ROOT );
    NS_ENSURE_SUCCESS( rv, rv );
  }
  return mdrCurrentArtist->GetStringValue(aCurrentArtist);
}

NS_IMETHODIMP
sbRemotePlayer::GetCurrentAlbum( nsAString &aCurrentAlbum )
{
  LOG(("sbRemotePlayer::GetCurrentAlbum()"));
  if (!mdrCurrentAlbum) {
    nsresult rv;
    mdrCurrentAlbum =
           do_CreateInstance( "@songbirdnest.com/Songbird/DataRemote;1", &rv );
    NS_ENSURE_SUCCESS( rv, rv );
    rv = mdrCurrentAlbum->Init( NS_LITERAL_STRING("metadata.album"),
                                SB_PREFS_ROOT );
    NS_ENSURE_SUCCESS( rv, rv );
  }
  return mdrCurrentAlbum->GetStringValue(aCurrentAlbum);
}

NS_IMETHODIMP
sbRemotePlayer::GetCurrentTrack( nsAString &aCurrentTrack )
{
  LOG(("sbRemotePlayer::GetCurrentTrack()"));
  if (!mdrCurrentTrack) {
    nsresult rv;
    mdrCurrentTrack =
           do_CreateInstance( "@songbirdnest.com/Songbird/DataRemote;1", &rv );
    NS_ENSURE_SUCCESS( rv, rv );
    rv = mdrCurrentTrack->Init( NS_LITERAL_STRING("metadata.title"),
                                SB_PREFS_ROOT );
    NS_ENSURE_SUCCESS( rv, rv );
  }
  return mdrCurrentTrack->GetStringValue(aCurrentTrack);
}

NS_IMETHODIMP
sbRemotePlayer::GetPlaying( PRBool *aPlaying )
{
  LOG(("sbRemotePlayer::GetPlaying()"));
  NS_ENSURE_ARG_POINTER(aPlaying);
  if (!mdrPlaying) {
    nsresult rv;
    mdrPlaying = do_CreateInstance( "@songbirdnest.com/Songbird/DataRemote;1",
                                    &rv );
    NS_ENSURE_SUCCESS( rv, rv );
    rv = mdrPlaying->Init( NS_LITERAL_STRING("faceplate.playing"),
                           SB_PREFS_ROOT );
    NS_ENSURE_SUCCESS( rv, rv );
  }
  return mdrPlaying->GetBoolValue(aPlaying);
}

NS_IMETHODIMP
sbRemotePlayer::GetPaused( PRBool *aPaused )
{
  LOG(("sbRemotePlayer::GetPaused()"));
  NS_ENSURE_ARG_POINTER(aPaused);
  if (!mdrPaused) {
    nsresult rv;
    mdrPaused = do_CreateInstance( "@songbirdnest.com/Songbird/DataRemote;1",
                                    &rv );
    NS_ENSURE_SUCCESS( rv, rv );
    rv = mdrPaused->Init( NS_LITERAL_STRING("faceplate.paused"),
                          SB_PREFS_ROOT );
    NS_ENSURE_SUCCESS( rv, rv );
  }
  return mdrPaused->GetBoolValue(aPaused);
}

NS_IMETHODIMP
sbRemotePlayer::GetRepeat( PRInt64 *aRepeat )
{
  LOG(("sbRemotePlayer::GetRepeat()"));
  NS_ENSURE_ARG_POINTER(aRepeat);
  if (!mdrRepeat) {
    nsresult rv;
    mdrRepeat = do_CreateInstance( "@songbirdnest.com/Songbird/DataRemote;1",
                                   &rv );
    NS_ENSURE_SUCCESS( rv, rv );
    rv = mdrRepeat->Init( NS_LITERAL_STRING("playlist.repeat"),
                          SB_PREFS_ROOT );
    NS_ENSURE_SUCCESS( rv, rv );
  }
  return mdrRepeat->GetIntValue(aRepeat);
}

NS_IMETHODIMP
sbRemotePlayer::GetShuffle( PRBool *aShuffle )
{
  LOG(("sbRemotePlayer::GetShuffle()"));
  NS_ENSURE_ARG_POINTER(aShuffle);
  if (!mdrShuffle) {
    nsresult rv;
    mdrShuffle = do_CreateInstance( "@songbirdnest.com/Songbird/DataRemote;1",
                                    &rv );
    NS_ENSURE_SUCCESS( rv, rv );
    rv = mdrShuffle->Init( NS_LITERAL_STRING("playlist.shuffle"),
                           SB_PREFS_ROOT );
    NS_ENSURE_SUCCESS( rv, rv );
  }
  return mdrShuffle->GetBoolValue(aShuffle);
}

NS_IMETHODIMP
sbRemotePlayer::GetPosition( PRInt64 *aPosition )
{
  LOG(("sbRemotePlayer::GetPosition()"));
  NS_ENSURE_ARG_POINTER(aPosition);
  if (!mdrPosition) {
    nsresult rv;
    mdrPosition = do_CreateInstance( "@songbirdnest.com/Songbird/DataRemote;1",
                                     &rv );
    NS_ENSURE_SUCCESS( rv, rv );
    rv = mdrPosition->Init( NS_LITERAL_STRING("metadata.position"),
                            SB_PREFS_ROOT );
    NS_ENSURE_SUCCESS( rv, rv );
  }
  return mdrPosition->GetIntValue(aPosition);
}

NS_IMETHODIMP
sbRemotePlayer::SetPosition( PRInt64 aPosition )
{
  LOG(("sbRemotePlayer::SetPosition()"));
  NS_ENSURE_ARG_POINTER(aPosition);

  nsresult rv = ConfirmPlaybackControl();
  NS_ENSURE_SUCCESS( rv, rv );

  nsCOMPtr<sbIMediacoreManager> mediaCoreMgr =
    do_GetService(SB_MEDIACOREMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediacorePlaybackControl> playbackControl;
  rv = mediaCoreMgr->GetPlaybackControl(getter_AddRefs(playbackControl));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = playbackControl->SetPosition(aPosition);
  NS_ENSURE_SUCCESS(rv, rv);

  return TakePlaybackControl( nsnull );
}

NS_IMETHODIMP
sbRemotePlayer::GetDuration( PRUint64 *aDuration )
{
  LOG(("sbRemotePlayer::GetDuration()"));
  NS_ENSURE_ARG_POINTER(aDuration);
  
  nsresult rv;
  nsCOMPtr<sbIMediacoreManager> mediaCoreMgr =
    do_GetService(SB_MEDIACOREMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediacorePlaybackControl> playbackControl;
  rv = mediaCoreMgr->GetPlaybackControl(getter_AddRefs(playbackControl));
  NS_ENSURE_SUCCESS(rv, rv);

  *aDuration = 0;

  if ( playbackControl ) {
    rv = playbackControl->GetDuration(aDuration);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::GetVolume( PRInt64 *aVolume )
{
  LOG(("sbRemotePlayer::GetVolume()"));
  NS_ENSURE_ARG_POINTER(aVolume);
  
  nsresult rv;
  nsCOMPtr<sbIMediacoreManager> mediaCoreMgr =
    do_GetService(SB_MEDIACOREMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  
  nsCOMPtr<sbIMediacoreVolumeControl> volumeControl;
  rv = mediaCoreMgr->GetVolumeControl(getter_AddRefs(volumeControl));
  NS_ENSURE_SUCCESS(rv, rv);
  
  double volume;
  rv = volumeControl->GetVolume(&volume);
  NS_ENSURE_SUCCESS(rv, rv);
  
  *aVolume = (PRInt64)(volume * 255); /* normalize to 0..255 */
  // constrain things, because the mediacore API says they can be out of range...
  if (NS_UNLIKELY(*aVolume < 0)) {
    *aVolume = 0;
  }
  if (NS_UNLIKELY(*aVolume > 255)) {
    *aVolume = 255;
  }
  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::GetMute( PRBool *aMute )
{
  LOG(("sbRemotePlayer::GetMute()"));
  NS_ENSURE_ARG_POINTER(aMute);
  if (!mdrMute) {
    nsresult rv;
    mdrMute = do_CreateInstance( "@songbirdnest.com/Songbird/DataRemote;1",
                                 &rv );
    NS_ENSURE_SUCCESS( rv, rv );
    rv = mdrMute->Init( NS_LITERAL_STRING("faceplate.mute"), SB_PREFS_ROOT );
    NS_ENSURE_SUCCESS( rv, rv );
  }
  return mdrMute->GetBoolValue(aMute);
}

NS_IMETHODIMP
sbRemotePlayer::AddListener( const nsAString &aKey,
                             sbIRemoteObserver *aObserver )
{
  LOG(("sbRemotePlayer::AddListener()"));
  NS_ENSURE_ARG_POINTER(aObserver);

  // Make sure the key passed in is a valid one for attaching to
  PRInt32 length = NS_ARRAY_LENGTH(sPublicMetadata);
  for (PRInt32 index = 0 ; index < length; index++ ) {
    // if we find it break out of the loop and keep going
    if ( aKey.EqualsLiteral(sPublicMetadata[index]) )
      break;
    // if we get to this point it isn't accepted, shortcircuit
    if ( index == (length-1) )
      return NS_ERROR_FAILURE;
  }

  nsresult rv;
  nsCOMPtr<sbIDataRemote> dr =
           do_CreateInstance( "@songbirdnest.com/Songbird/DataRemote;1", &rv );
  NS_ENSURE_SUCCESS( rv, rv );
  rv = dr->Init( aKey, SB_PREFS_ROOT );
  NS_ENSURE_SUCCESS( rv, rv );
  rv = dr->BindRemoteObserver( aObserver, PR_FALSE );
  NS_ENSURE_SUCCESS( rv, rv );

  sbRemoteObserver remObs;
  remObs.observer = aObserver;
  remObs.remote = dr;
  PRBool success = mRemObsHash.Put( aKey, remObs );
  NS_ENSURE_TRUE( success, NS_ERROR_OUT_OF_MEMORY );

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::RemoveListener( const nsAString &aKey,
                                sbIRemoteObserver *aObserver )
{
  NS_ENSURE_ARG_POINTER(aObserver);
  LOG(("sbRemotePlayer::RemoveListener(%s %x)",
       PromiseFlatString(aKey).get(), aObserver));

  sbRemoteObserver remObs;
  mRemObsHash.Get( aKey, &remObs );

  if ( remObs.observer == aObserver ) {
    LOG(( "sbRemotePlayer::RemoveListener(%s %x) -- found observer",
          NS_LossyConvertUTF16toASCII(aKey).get(), aObserver ));

    remObs.remote->Unbind();
    mRemObsHash.Remove(aKey);
  }
  else {
    LOG(( "sbRemotePlayer::RemoveListener(%s %x) -- did NOT find observer",
          PromiseFlatString(aKey).get(), aObserver ));
  }

  return NS_OK;
}

static inline
nsresult StandardPlay(nsIWeakReference *aWeakRef) {
  NS_ENSURE_ARG_POINTER(aWeakRef);

  // XXXAus: Get Main Library from Library Manager.
  // XXXAus: Get View from Main Library.
  // XXXAus: Play Main Library View.

  nsresult rv;
  nsCOMPtr<sbIMediacoreManager> manager = 
    do_QueryReferent(aWeakRef, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::Play()
{
  LOG(("sbRemotePlayer::Play()"));
  NS_ENSURE_STATE(mMM);

  nsresult rv = ConfirmPlaybackControl();
  NS_ENSURE_SUCCESS( rv, rv );

  // Check to see if playback is currently paused.
  // If currently paused, first attempt to resume.
  PRBool isPaused = PR_FALSE;

  rv = GetPaused( &isPaused );
  NS_ENSURE_SUCCESS( rv, rv );

  if ( isPaused ) {
    nsCOMPtr<sbIMediacoreManager> manager = 
      do_QueryReferent(mMM, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbIMediacorePlaybackControl> playbackControl;
    rv = manager->GetPlaybackControl(getter_AddRefs(playbackControl));
    NS_ENSURE_SUCCESS(rv, rv);

    if ( playbackControl ) {
      rv = playbackControl->Play();
      NS_ENSURE_SUCCESS(rv, rv);
    }

    return NS_OK;
  }

  if (!mRemWebPlaylist) {
    rv = InitRemoteWebPlaylist();
    NS_ENSURE_SUCCESS( rv, rv );
  }

  nsCOMPtr<sbIMediaListView> mediaListView;
  rv = mRemWebPlaylist->GetListView( getter_AddRefs(mediaListView) );
  NS_ENSURE_SUCCESS( rv, rv );

  // If the page does not have a web playlist, fall back
  // to the standard play button functionality.
  if (!mediaListView) {
    return StandardPlay(mMM);
  }

  nsCOMPtr<nsITreeView> treeView;
  rv = mediaListView->GetTreeView( getter_AddRefs(treeView) );
  if ( NS_FAILED(rv) ) {
    NS_WARNING("Got list view but did not get tree view! Falling back to standard play.");
    return StandardPlay(mMM);
  }

  nsCOMPtr<nsITreeSelection> treeSelection;
  rv = treeView->GetSelection( getter_AddRefs(treeSelection) );
  if ( NS_FAILED(rv) || !treeSelection ) {
    NS_WARNING("Got tree view but did not get selection in view. Falling back to standard play.");
    return StandardPlay(mMM);
  }

  PRInt32 index;
  treeSelection->GetCurrentIndex(&index);
  if ( index < 0 )
    index = 0;

  nsCOMPtr<sbIMediacoreManager> manager = 
    do_QueryReferent(mMM, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediacoreSequencer> sequencer;
  rv = manager->GetSequencer(getter_AddRefs(sequencer));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = sequencer->PlayView( mediaListView, index, PR_FALSE);
  NS_ENSURE_SUCCESS( rv, rv );

  rv = TakePlaybackControl( nsnull );
  NS_ENSURE_SUCCESS( rv, rv );

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::PlayMediaList( sbIRemoteMediaList *aList, PRInt32 aIndex )
{
  LOG(("sbRemotePlayer::PlayMediaList()"));
  NS_ENSURE_ARG_POINTER(aList);
  NS_ENSURE_STATE(mMM);

  nsresult rv = ConfirmPlaybackControl();
  NS_ENSURE_SUCCESS( rv, rv );

  // Get the existing view if there is one
  nsCOMPtr<sbIMediaListView> mediaListView;
  rv = aList->GetView( getter_AddRefs(mediaListView) );

  // There isn't a view so create one
  if (!mediaListView) {
    nsCOMPtr<sbIMediaList> list( do_QueryInterface( aList, &rv ) );
    NS_ENSURE_SUCCESS( rv, rv );

    rv = list->CreateView( nsnull, getter_AddRefs(mediaListView) );
    NS_ENSURE_SUCCESS( rv, rv );
  }

  if ( aIndex < 0 )
    aIndex = 0;

  nsCOMPtr<sbIMediacoreManager> manager = 
    do_QueryReferent(mMM, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediacoreSequencer> sequencer;
  rv = manager->GetSequencer(getter_AddRefs(sequencer));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = sequencer->PlayView( mediaListView, aIndex, PR_FALSE);
  NS_ENSURE_SUCCESS( rv, rv );

  rv = TakePlaybackControl( nsnull );
  NS_ENSURE_SUCCESS( rv, rv );

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::PlayURL( const nsAString &aURL )
{
  LOG(("sbRemotePlayer::PlayURL()"));
  NS_ENSURE_STATE(mMM);

  nsresult rv = ConfirmPlaybackControl();
  NS_ENSURE_SUCCESS( rv, rv );

  nsCOMPtr<sbIMediacoreManager> manager = 
    do_QueryReferent(mMM, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediacoreSequencer> sequencer;
  rv = manager->GetSequencer(getter_AddRefs(sequencer));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURI> uri;
  rv = mIOService->NewURI(NS_ConvertUTF16toUTF8(aURL), 
                          nsnull, 
                          nsnull, 
                          getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = sequencer->PlayURL(uri);
  NS_ENSURE_SUCCESS( rv, rv );

  rv = TakePlaybackControl( nsnull );
  NS_ENSURE_SUCCESS( rv, rv );

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::Stop()
{
  LOG(("sbRemotePlayer::Stop()"));
  NS_ENSURE_STATE(mMM);

  nsresult rv = ConfirmPlaybackControl();
  NS_ENSURE_SUCCESS( rv, rv );

  nsCOMPtr<sbIMediacoreManager> manager = 
    do_QueryReferent(mMM, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediacorePlaybackControl> playbackControl;
  rv = manager->GetPlaybackControl(getter_AddRefs(playbackControl));
  NS_ENSURE_SUCCESS(rv, rv);

  if ( playbackControl ) {
    rv = playbackControl->Stop();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = TakePlaybackControl( nsnull );
  NS_ENSURE_SUCCESS( rv, rv );

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::Pause()
{
  LOG(("sbRemotePlayer::Pause()"));
  NS_ENSURE_STATE(mMM);

  nsresult rv = ConfirmPlaybackControl();
  NS_ENSURE_SUCCESS( rv, rv );

  nsCOMPtr<sbIMediacoreManager> manager = 
    do_QueryReferent(mMM, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediacorePlaybackControl> playbackControl;
  rv = manager->GetPlaybackControl(getter_AddRefs(playbackControl));
  NS_ENSURE_SUCCESS(rv, rv);

  if ( playbackControl ) {
    rv = playbackControl->Pause();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = TakePlaybackControl( nsnull );
  NS_ENSURE_SUCCESS( rv, rv );

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::Next()
{
  LOG(("sbRemotePlayer::Next()"));
  NS_ENSURE_STATE(mMM);

  nsresult rv = ConfirmPlaybackControl();
  NS_ENSURE_SUCCESS( rv, rv );

  nsCOMPtr<sbIMediacoreManager> manager = 
    do_QueryReferent(mMM, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediacoreSequencer> sequencer;
  rv = manager->GetSequencer(getter_AddRefs(sequencer));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = sequencer->Next(PR_FALSE);
  NS_ENSURE_SUCCESS( rv, rv );

  rv = TakePlaybackControl( nsnull );
  NS_ENSURE_SUCCESS( rv, rv );

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::Previous()
{
  LOG(("sbRemotePlayer::Previous()"));
  NS_ENSURE_STATE(mMM);

  nsresult rv = ConfirmPlaybackControl();
  NS_ENSURE_SUCCESS( rv, rv );

  nsCOMPtr<sbIMediacoreManager> manager = 
    do_QueryReferent(mMM, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediacoreSequencer> sequencer;
  rv = manager->GetSequencer(getter_AddRefs(sequencer));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = sequencer->Previous(PR_FALSE);
  NS_ENSURE_SUCCESS( rv, rv );

  rv = TakePlaybackControl( nsnull );
  NS_ENSURE_SUCCESS( rv, rv );

  return NS_OK;
}

nsresult
sbRemotePlayer::FireEventToContent( const nsAString &aClass,
                                    const nsAString &aType )
{
  LOG(( "sbRemotePlayer::FireEventToContent(%s, %s)",
        NS_LossyConvertUTF16toASCII(aClass).get(),
        NS_LossyConvertUTF16toASCII(aType).get() ));

  return sbRemotePlayer::DispatchEvent(mContentDoc, aClass, aType, PR_FALSE);
}

NS_IMETHODIMP
sbRemotePlayer::FireMediaItemStatusEventToContent( const nsAString &aClass,
                                                   const nsAString &aType,
                                                   sbIMediaItem* aMediaItem,
                                                   PRInt32 aStatus )
{
  LOG(( "sbRemotePlayer::FireMediaItemStatusEventToContent(%s, %s)",
        NS_LossyConvertUTF16toASCII(aClass).get(),
        NS_LossyConvertUTF16toASCII(aType).get() ));
  NS_ENSURE_ARG_POINTER(aMediaItem);

  nsresult rv;

  nsCOMPtr<sbISecurityMixin> mixin = do_QueryInterface(mSecurityMixin, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDOMDocument> doc;
  mixin->GetNotificationDocument(getter_AddRefs(doc));
  NS_ENSURE_STATE(doc);

  //change interfaces to create the event
  nsCOMPtr<nsIDOMDocumentEvent>
                              docEvent( do_QueryInterface( doc, &rv ) );
  NS_ENSURE_SUCCESS( rv , rv );

  //create the event
  nsCOMPtr<nsIDOMEvent> event;
  docEvent->CreateEvent( aClass, getter_AddRefs(event) );
  NS_ENSURE_STATE(event);
  rv = event->InitEvent( aType, PR_TRUE, PR_TRUE );
  NS_ENSURE_SUCCESS( rv , rv );

  //use the document for a target.
  nsCOMPtr<nsIDOMEventTarget>
                          eventTarget( do_QueryInterface( doc, &rv ) );
  NS_ENSURE_SUCCESS( rv, rv );

  //make the event untrusted
  nsCOMPtr<nsIPrivateDOMEvent> privEvt( do_QueryInterface( event, &rv ) );
  NS_ENSURE_SUCCESS( rv, rv );
  privEvt->SetTrusted(PR_FALSE);

  // make our custom media item wrapper event
  nsRefPtr<sbRemoteMediaItemStatusEvent>
                          remoteEvent(new sbRemoteMediaItemStatusEvent(this) );
  rv = remoteEvent->Init();
  NS_ENSURE_SUCCESS( rv, rv );
  rv = remoteEvent->InitEvent( event, aMediaItem, aStatus );
  NS_ENSURE_SUCCESS( rv, rv );

  // Fire an event to the chrome system.
  PRBool dummy;
  return eventTarget->DispatchEvent( remoteEvent, &dummy );
}

// ---------------------------------------------------------------------------
//
//                           sbIMediacoreEventListener
//
// ---------------------------------------------------------------------------

NS_IMETHODIMP
sbRemotePlayer::OnMediacoreEvent(sbIMediacoreEvent *aEvent) 
{
  NS_ENSURE_ARG_POINTER(aEvent);

  PRUint32 type = 0;
  nsresult rv = aEvent->GetType(&type);
  NS_ENSURE_SUCCESS(rv, rv);

  switch(type) {
    case sbIMediacoreEvent::STREAM_END:
    case sbIMediacoreEvent::STREAM_STOP: {
      rv = OnStop();
      NS_ENSURE_SUCCESS(rv, rv);
    }
    break;

    case sbIMediacoreEvent::TRACK_CHANGE: {
      rv = OnTrackChange(aEvent);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    break;

    case sbIMediacoreEvent::TRACK_INDEX_CHANGE: {
      rv = OnTrackIndexChange(aEvent);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    break;

    case sbIMediacoreEvent::BEFORE_VIEW_CHANGE: {
      rv = OnBeforeViewChange(aEvent);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    break;

    case sbIMediacoreEvent::VIEW_CHANGE: {
      rv = OnViewChange(aEvent);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    break;
  }

  return NS_OK;
}

// ---------------------------------------------------------------------------
//
//                           Event Handlers for sbIMediacoreEventListener
//
// ---------------------------------------------------------------------------
nsresult
sbRemotePlayer::OnStop()
{
  LOG(("sbRemotePlayer::OnStop()"));
  nsresult rv;

  rv = FireEventToContent( RAPI_EVENT_CLASS, RAPI_EVENT_TYPE_STOP );
  NS_ENSURE_SUCCESS( rv, rv );

  return NS_OK;
}

nsresult
sbRemotePlayer::OnBeforeTrackChange(sbIMediacoreEvent *aEvent)
{
  LOG(("sbRemotePlayer::OnBeforeTrackChange()"));
  NS_ENSURE_ARG_POINTER(aEvent);

  nsCOMPtr<nsIVariant> data;
  nsresult rv = aEvent->GetData(getter_AddRefs(data));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISupports> supports;
  rv = data->GetAsISupports(getter_AddRefs(supports));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediaItem> item = do_QueryInterface(supports, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = FireMediaItemStatusEventToContent( RAPI_EVENT_CLASS,
                                          RAPI_EVENT_TYPE_BEFORETRACKCHANGE,
                                          item,
                                          NS_OK );
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult 
sbRemotePlayer::OnTrackChange(sbIMediacoreEvent *aEvent)
{
  LOG(("sbRemotePlayer::OnTrackChange()"));
  NS_ENSURE_ARG_POINTER(aEvent);

  nsCOMPtr<nsIVariant> data;
  nsresult rv = aEvent->GetData(getter_AddRefs(data));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISupports> supports;
  rv = data->GetAsISupports(getter_AddRefs(supports));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediaItem> item = do_QueryInterface(supports, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = FireMediaItemStatusEventToContent( RAPI_EVENT_CLASS,
                                          RAPI_EVENT_TYPE_TRACKCHANGE,
                                          item,
                                          NS_OK );
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbRemotePlayer::OnTrackIndexChange(sbIMediacoreEvent *aEvent) {
  LOG(("sbRemotePlayer::OnTrackIndexChange()"));
  NS_ENSURE_ARG_POINTER(aEvent);

  nsCOMPtr<nsIVariant> data;
  nsresult rv = aEvent->GetData(getter_AddRefs(data));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISupports> supports;
  rv = data->GetAsISupports(getter_AddRefs(supports));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<sbIMediaItem> item = do_QueryInterface(supports, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = FireMediaItemStatusEventToContent( RAPI_EVENT_CLASS,
                                          RAPI_EVENT_TYPE_TRACKINDEXCHANGE,
                                          item,
                                          NS_OK );
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbRemotePlayer::OnBeforeViewChange(sbIMediacoreEvent *aEvent) {
  LOG(("sbRemotePlayer::OnViewChange()"));
  NS_ENSURE_ARG_POINTER(aEvent);

  nsresult rv = FireEventToContent( RAPI_EVENT_CLASS, 
                                    RAPI_EVENT_TYPE_BEFOREVIEW );
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
sbRemotePlayer::OnViewChange(sbIMediacoreEvent *aEvent) {
  LOG(("sbRemotePlayer::OnViewChange()"));
  NS_ENSURE_ARG_POINTER(aEvent);

  nsresult rv = FireEventToContent( RAPI_EVENT_CLASS, 
                                    RAPI_EVENT_TYPE_VIEW );
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

// ---------------------------------------------------------------------------
//
//                           nsIDOMEventListener
//
// ---------------------------------------------------------------------------

NS_IMETHODIMP
sbRemotePlayer::HandleEvent( nsIDOMEvent *aEvent )
{
  LOG(( "sbRemotePlayer::HandleEvent() this: %08x", this ));
  NS_ENSURE_ARG_POINTER(aEvent);
  nsAutoString type;
  aEvent->GetType(type);
  if ( type.EqualsLiteral("unload") ) {
    LOG(("sbRemotePlayer::HandleEvent() - unload event"));
    nsresult rv;

    nsRefPtr<sbRemotePlayer> kungFuDeathGrip(this);

    // check if this is the right document being unloaded (yay tabs)
    nsCOMPtr<nsIDOMNSEvent> nsEvent( do_QueryInterface( aEvent, &rv ) );
    if ( NS_FAILED(rv) )
      return NS_OK;

    nsCOMPtr<nsIDOMEventTarget> originalEventTarget;
    rv = nsEvent->GetOriginalTarget( getter_AddRefs(originalEventTarget) );
    NS_ENSURE_SUCCESS( rv, rv );

    if ( !SameCOMIdentity( originalEventTarget, mContentDoc ) ) {
      // not the content doc we're interested in
      return NS_OK;
    }

    nsCOMPtr<nsIDOMEventTarget> eventTarget( do_QueryInterface(mChromeDoc) );
    NS_ENSURE_STATE(eventTarget);
    rv = eventTarget->RemoveEventListener( NS_LITERAL_STRING("unload"),
                                           this ,
                                           PR_TRUE );
    NS_ASSERTION( NS_SUCCEEDED(rv),
                  "Failed to remove unload listener from document" );

    rv = eventTarget->RemoveEventListener( NS_LITERAL_STRING("PlaylistCellClick"),
                                           this ,
                                           PR_TRUE );
    NS_ASSERTION( NS_SUCCEEDED(rv),
                  "Failed to remove PlaylistCellClick listener from document" );

    rv = eventTarget->RemoveEventListener( SB_EVENT_RAPI_PERMISSION_DENIED,
                                           this ,
                                           PR_TRUE );
    NS_ASSERTION( NS_SUCCEEDED(rv),
                  "Failed to remove RemoteAPIPermissionDenied listener from document" );

    rv = eventTarget->RemoveEventListener( SB_EVENT_RAPI_PERMISSION_CHANGED,
                                           this ,
                                           PR_TRUE );
    NS_ASSERTION( NS_SUCCEEDED(rv),
                  "Failed to remove RemoteAPIPermissionChanged listener from document" );

    nsCOMPtr<sbIMediacoreEventTarget> target = 
      do_QueryReferent(mMM, &rv);
    NS_ASSERTION( NS_SUCCEEDED(rv), 
      "Failed to get event target to remove playback listener" );

    rv = target->RemoveListener(this);
    NS_ASSERTION( NS_SUCCEEDED(rv), "Failed to remove playback listener" );

    // the page is going away, clean up things that will cause us to
    // not get released.
    UnregisterCommands();
    mCommandsObject = nsnull;
    mContentDoc = nsnull;
    mChromeDoc = nsnull;

    // Explicitly clear the cached libraries so we can break the reference
    // cycle between the library classes and this class
    mCachedLibraries.Clear();
  } else if ( type.EqualsLiteral("PlaylistCellClick") ) {
    LOG(("sbRemotePlayer::HandleEvent() - PlaylistCellClick event"));
    nsresult rv;

    // get the event information from the playlist
    nsCOMPtr<nsIDOMNSEvent> srcEvent( do_QueryInterface(aEvent, &rv) );
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIDOMEventTarget> srcEventTarget;
    rv = srcEvent->GetOriginalTarget( getter_AddRefs(srcEventTarget) );
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<sbIPlaylistWidget> playlist( do_QueryInterface(srcEventTarget, &rv) );
    // this recurses because we see the event targeted at the content document
    // bubble up, so just gracefully ignore it if the event wasn't from a playlist
    if (NS_FAILED(rv))
      return NS_OK;

    if (!mRemWebPlaylist) {
      rv = InitRemoteWebPlaylist();
      NS_ENSURE_SUCCESS( rv, rv );
    }

    // Get the actual XBL widget from the RemoteWebPlaylist member
    nsCOMPtr<sbIPlaylistWidget> playlistWidget;
    rv = mRemWebPlaylist->GetPlaylistWidget( getter_AddRefs(playlistWidget) );
    NS_ENSURE_SUCCESS(rv, rv);

    // check if this is the right document being clicked on (yay tabs)
    if ( !SameCOMIdentity( playlist, playlistWidget ) ) {
      // not the playlist for this RemotePlayer
      return NS_OK;
    }

    nsCOMPtr<sbIPlaylistClickEvent> playlistClickEvent;
    rv = playlist->GetLastClickEvent( getter_AddRefs(playlistClickEvent) );
    NS_ENSURE_SUCCESS(rv, rv);

    // make a mouse event
    nsCOMPtr<nsIDOMDocumentEvent> docEvent( do_QueryInterface(mContentDoc, &rv) );
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIDOMEvent> newEvent;
    rv = docEvent->CreateEvent( NS_LITERAL_STRING("mouseevent"), getter_AddRefs(newEvent) );
    NS_ENSURE_SUCCESS(rv, rv);

    PRBool ctrlKey = PR_FALSE, altKey = PR_FALSE,
           shiftKey = PR_FALSE, metaKey = PR_FALSE;
    PRUint16 button = 0;
    nsCOMPtr<nsIDOMMouseEvent> srcMouseEvent( do_QueryInterface(playlistClickEvent, &rv) );
    if (NS_SUCCEEDED(rv)) {
      srcMouseEvent->GetCtrlKey(&ctrlKey);
      srcMouseEvent->GetAltKey(&altKey);
      srcMouseEvent->GetShiftKey(&shiftKey);
      srcMouseEvent->GetMetaKey(&metaKey);
      srcMouseEvent->GetButton(&button);
    } else {
      LOG(("sbRemotePlayer::HandleEvent() - no mouse event for PlaylistCellClick"));
    }

    nsCOMPtr<nsIDOMMouseEvent> newMouseEvent( do_QueryInterface(newEvent, &rv) );
    NS_ENSURE_SUCCESS(rv, rv);
    rv = newMouseEvent->InitMouseEvent( NS_LITERAL_STRING("PlaylistCellClick"),
                                        PR_TRUE, /* can bubble */
                                        PR_TRUE, /* can cancel */
                                        nsnull,
                                        0, /* detail */
                                        0, 0, /* screen x & y */
                                        0, 0, /* client x & y */
                                        ctrlKey, /* ctrl key */
                                        altKey, /* alt key */
                                        shiftKey, /* shift key */
                                        metaKey, /* meta key */
                                        button, /* button */
                                        nsnull /* related target */
                                      );
    NS_ENSURE_SUCCESS(rv, rv);

    //make the event trusted
    nsCOMPtr<nsIPrivateDOMEvent> privEvt( do_QueryInterface( newEvent, &rv ) );
    NS_ENSURE_SUCCESS( rv, rv );
    privEvt->SetTrusted( PR_TRUE );

    // make our custom wrapper event
    nsRefPtr<sbRemotePlaylistClickEvent> remoteEvent( new sbRemotePlaylistClickEvent(this) );
    NS_ENSURE_TRUE(remoteEvent, NS_ERROR_OUT_OF_MEMORY);

    rv = remoteEvent->Init();
    NS_ENSURE_SUCCESS(rv, rv);

    rv = remoteEvent->InitEvent( playlistClickEvent, newMouseEvent );
    NS_ENSURE_SUCCESS(rv, rv);

    // dispatch the event the the content document
    PRBool dummy;
    nsCOMPtr<nsIDOMEventTarget> destEventTarget = do_QueryInterface( mContentDoc, &rv );
    NS_ENSURE_SUCCESS(rv, rv);
    rv = destEventTarget->DispatchEvent( remoteEvent, &dummy );
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    // The 2 cases we want to pass the event on.
    //   1) The target was the chrome document, happens when the event comes
    //      from the preferences panel
    //   2) The target was a XUL Element, happens when the event comes from
    //      the notification box.
    // The prefPanel event goes to all tabs since it is general information
    //   that could impact any page. The notification event only goes to the
    //   page that caused the notification.

    // Get the original target
    nsresult rv;
    nsCOMPtr<nsIDOMNSEvent> nsEvent( do_QueryInterface( aEvent, &rv ) );
    if ( NS_FAILED(rv) )
      return NS_OK;

    nsCOMPtr<nsIDOMEventTarget> originalEventTarget;
    rv = nsEvent->GetOriginalTarget( getter_AddRefs(originalEventTarget) );
    NS_ENSURE_SUCCESS( rv, rv );

    PRBool allow = PR_FALSE;
    if ( SameCOMIdentity( originalEventTarget, mChromeDoc ) ) {
      LOG(( "    - target IS this chromeDoc - ALLOWING " ));
      allow = PR_TRUE;
    }

    nsCOMPtr<nsIDOMXULElement> domXULElement (
      do_QueryInterface(originalEventTarget) );
    if (domXULElement) {
      // this is the notifcation box sending us an event. make sure it is ours

      // Get the notifcation box for the notification that is the target
      nsCOMPtr<nsIDOMNode> targetNotification (
        do_QueryInterface(originalEventTarget) );
      NS_ENSURE_TRUE( targetNotification, NS_ERROR_OUT_OF_MEMORY );

      nsCOMPtr<nsIDOMNode> targetNotificationBox;
      rv = targetNotification->GetParentNode(
        getter_AddRefs(targetNotificationBox) );
      NS_ENSURE_SUCCESS( rv, rv );

      // Get the notification associated with our tab/browser
      nsCOMPtr<nsIDOMElement> selfBrowser;
      rv = GetBrowser( getter_AddRefs(selfBrowser) );
      NS_ENSURE_SUCCESS( rv, rv );

      nsCOMPtr<nsIDOMNode> selfBrowserNode ( do_QueryInterface( selfBrowser ) );
      NS_ENSURE_TRUE( selfBrowserNode, NS_ERROR_OUT_OF_MEMORY );

      nsCOMPtr<nsIDOMNode> selfNotificationBox;
      rv = selfBrowserNode->GetParentNode(
        getter_AddRefs(selfNotificationBox) );
      NS_ENSURE_SUCCESS( rv, rv );

      if ( SameCOMIdentity( selfNotificationBox, targetNotificationBox ) ) {
        LOG(( "    - target IS this notificationbox - ALLOWING " ));
        allow = PR_TRUE;
      }
    }

    if ( allow ) {

      nsString value;
      if ( type.Equals( SB_EVENT_RAPI_PERMISSION_DENIED ) ) {
        value.AssignLiteral("dismissed");
      }
      else {
        value.AssignLiteral("preferences");
      }

      nsCOMPtr<sbIRemoteSecurityEvent> securityEvent =
        do_QueryInterface(aEvent, &rv);

      if ( NS_SUCCEEDED( rv ) ) {

        nsAutoString categoryID;
        rv = securityEvent->GetCategoryID(categoryID);
        NS_ENSURE_SUCCESS( rv, rv );

        PRBool hasAccess = PR_FALSE;
        rv = securityEvent->GetHasAccess(&hasAccess);
        NS_ENSURE_SUCCESS( rv, rv );

        nsCOMPtr<sbISecurityMixin> mixin = do_QueryInterface(mSecurityMixin, &rv);
        NS_ENSURE_SUCCESS( rv, rv );

        nsCOMPtr<nsIDOMDocument> doc;
        rv = mixin->GetNotificationDocument(getter_AddRefs(doc));
        NS_ENSURE_SUCCESS( rv, rv );

        rv = DispatchSecurityEvent(doc,
                                   this,
                                   RAPI_EVENT_CLASS,
                                   type,
                                   categoryID,
                                   hasAccess,
                                   PR_FALSE );
#ifdef METRICS_ENABLED
        rv = mMetrics->MetricsInc(NS_LITERAL_STRING("rapi.notification"),
                                  value,
                                  EmptyString());
        NS_ENSURE_SUCCESS( rv, rv );
#endif
      }
      else {
        LOG(( "sbRemotePlayer::HandleEvent() - %s",
          NS_LossyConvertUTF16toASCII(type).get() ));

        if ( type.Equals(SB_EVENT_RAPI_PERMISSION_CHANGED) ) {
          return FireEventToContent( RAPI_EVENT_CLASS, type );
        }
        else if ( type.Equals(SB_EVENT_RAPI_PERMISSION_DENIED) ) {
#ifdef METRICS_ENABLED
          rv = mMetrics->MetricsInc(NS_LITERAL_STRING("rapi.notification"),
                                    value,
                                    EmptyString());
          NS_ENSURE_SUCCESS( rv, rv );
#endif
          return FireEventToContent( RAPI_EVENT_CLASS, type );
        } else {
          LOG(("sbRemotePlayer::HandleEvent() - Some other event"));
        }
      }
    }
  }
  return NS_OK;
}

nsresult
sbRemotePlayer::UnregisterCommands()
{
  LOG(("sbRemotePlayer::UnregisterCommands()"));
  if (!mCommandsObject)
    return NS_OK;

  // Get the PlaylistCommandsManager object to unregister the commands
  nsresult rv;
  nsCOMPtr<sbIPlaylistCommandsManager> mgr(
         do_GetService( "@songbirdnest.com/Songbird/PlaylistCommandsManager;1", &rv ) );
  NS_ENSURE_SUCCESS( rv, rv );

  nsCOMPtr<sbIPlaylistCommands> commands = (sbIPlaylistCommands*) mCommandsObject;
  NS_ENSURE_TRUE( commands, NS_ERROR_UNEXPECTED );
  // Registration of commands is changing soon, for now type='library' is it
  rv = mgr->UnregisterPlaylistCommandsMediaItem( NS_LITERAL_STRING("remote-test-guid"),
                                                 NS_LITERAL_STRING("library"),
                                                 commands );
  NS_ASSERTION( NS_SUCCEEDED(rv),
                "Failed to unregister commands from playlistcommandsmanager" );

  rv = mgr->UnregisterPlaylistCommandsMediaItem( NS_LITERAL_STRING("remote-test-guid"),
                                                 NS_LITERAL_STRING("simple"),
                                                 commands );
  NS_ASSERTION( NS_SUCCEEDED(rv),
                "Failed to unregister commands from playlistcommandsmanager" );

  return NS_OK;
}

nsresult
sbRemotePlayer::ConfirmPlaybackControl() {
  // NOTE: Making this function always succeed.
  // See bugs 13368 and 13403
  return NS_OK;

  // the web site is trying to control the user's playback.
  // try to guess if this is likely to annoy the user.

  LOG(("sbRemotePlayer::ConfirmPlaybackControl()"));

  PRBool isPlaying;
  nsresult rv;

  // we are safe to run from untrusted script.  Since we may end up across
  // XPConnect and land in JS again, we need to push a principal so that when
  // XPConnect looks it sees that it's a chrome caller.

  // before we do that, however, we need to get any information we need from
  // the content document (since that will be unavailable when we push)
  nsCOMPtr<sbISecurityMixin> securityMixin =
    do_QueryInterface( mSecurityMixin, &rv );
  NS_ENSURE_SUCCESS( rv, rv );
  nsCOMPtr<nsIURI> codebaseURI;
  rv = securityMixin->GetCodebase( getter_AddRefs(codebaseURI) );
  NS_ENSURE_SUCCESS( rv, rv );

  // scope the principal push
  {
    // we can now push the new principal
    sbAutoPrincipalPusher principal;
    if (!principal) {
      return NS_ERROR_FAILURE;
    }

    rv = GetPlaying( &isPlaying );
    NS_ENSURE_SUCCESS( rv, rv );

    if (!isPlaying) {
      // The player is not playing, allow it
      LOG(("sbRemotePlayer::ConfirmPlaybackControl() -- not playing, allow"));
      return NS_OK;
    }

    // check to see if this page has control
    nsCOMPtr<sbIRemoteAPIService> remoteAPIService =
      do_GetService( "@songbirdnest.com/remoteapi/remoteapiservice;1", &rv );
    NS_ENSURE_SUCCESS( rv, rv );
    PRBool hasPlaybackControl;
    rv = remoteAPIService->HasPlaybackControl( codebaseURI, &hasPlaybackControl );
    NS_ENSURE_SUCCESS( rv, rv );
    if ( hasPlaybackControl ) {
      LOG(("sbRemotePlayer::ConfirmPlaybackControl() -- URI has control, allow"));
      return NS_OK;
    }
  } // pop the principal

  PRBool allowed =
    GetUserApprovalForHost( codebaseURI,
                            NS_LITERAL_STRING("rapi.playback_control.blocked.title"),
                            NS_LITERAL_STRING("rapi.playback_control.blocked.message") );

  return allowed ? NS_OK : NS_ERROR_ABORT ;
}

PRBool
sbRemotePlayer::GetUserApprovalForHost( nsIURI *aURI,
                                        const nsAString &aTitleKey,
                                        const nsAString &aMessageKey,
                                        const char* aScopedName )
{
  LOG(("sbRemotePlayer::GetUserApprovalForHost(URI)"));
  NS_ENSURE_ARG_POINTER(aURI);
  NS_ASSERTION(!aTitleKey.IsEmpty(), "Title Key empty!!!!");
  NS_ASSERTION(!aMessageKey.IsEmpty(), "Message key empty!!!!");

  // See if we should prompt for approval
  nsresult rv;
  nsCOMPtr<nsIPrefBranch> prefService =
    do_GetService("@mozilla.org/preferences-service;1", &rv);
  if (NS_SUCCEEDED(rv)) {
    PRBool shouldPrompt;
    rv = prefService->GetBoolPref( "songbird.rapi.promptForApproval",
                                   &shouldPrompt );
    if (NS_SUCCEEDED(rv) && !shouldPrompt) {
      LOG(("sbRemotePlayer::GetUserApprovalForHost(URI) - shouldn't prompt or failed"));
      return PR_FALSE;
    }
  }
  LOG(("sbRemotePlayer::GetUserApprovalForHost(URI) - here"));

  nsCString hostUTF8;
  rv = aURI->GetHost(hostUTF8);
  NS_ENSURE_SUCCESS( rv, rv );

  if ( hostUTF8.IsEmpty() ) {
    // no host (e.g. file:///); fall back to full URL
    rv = aURI->GetSpec(hostUTF8);
    NS_ENSURE_SUCCESS( rv, rv );
  }
  NS_ConvertUTF8toUTF16 host(hostUTF8);

  nsCOMPtr<nsIStringBundleService> sbs =
    do_GetService( NS_STRINGBUNDLE_CONTRACTID, &rv );
  NS_ENSURE_SUCCESS( rv, PR_FALSE );

  // get the branding...
  nsCOMPtr<nsIStringBundle> bundle;
  rv = sbs->CreateBundle( "chrome://branding/locale/brand.properties",
                          getter_AddRefs(bundle) );
  NS_ENSURE_SUCCESS( rv, PR_FALSE );

  nsString branding;
  rv = bundle->GetStringFromName( NS_LITERAL_STRING("brandShortName").get(),
                                  getter_Copies(branding) );
  NS_ENSURE_SUCCESS( rv, PR_FALSE );

  // and the prompt title and message
  rv = sbs->CreateBundle( "chrome://songbird/locale/songbird.properties",
                          getter_AddRefs(bundle) );
  NS_ENSURE_SUCCESS( rv, PR_FALSE );

  const PRUnichar *formatParams[1] = {0};
  formatParams[0] = branding.BeginReading();

  nsString message;
  rv = bundle->FormatStringFromName( aMessageKey.BeginReading(),
                                     formatParams,
                                     NS_ARRAY_LENGTH(formatParams),
                                     getter_Copies(message) );
  NS_ENSURE_SUCCESS( rv, PR_FALSE );

  nsString title;
  rv = bundle->GetStringFromName( aTitleKey.BeginReading(),
                                  getter_Copies(title) );
  NS_ENSURE_SUCCESS( rv, PR_FALSE );

  nsString allowDeny;
  rv = bundle->GetStringFromName( NS_LITERAL_STRING("rapi.permissions.allow.deny").get(),
                                  getter_Copies(allowDeny) );
  NS_ENSURE_SUCCESS( rv, PR_FALSE );

  nsString allowAlways;
  rv = bundle->GetStringFromName( NS_LITERAL_STRING("rapi.permissions.allow.always").get(),
                                  getter_Copies(allowAlways) );
  NS_ENSURE_SUCCESS( rv, PR_FALSE );

  nsString allowOnce;
  rv = bundle->GetStringFromName( NS_LITERAL_STRING("rapi.permissions.allow.once").get(),
                                  getter_Copies(allowOnce) );
  NS_ENSURE_SUCCESS( rv, PR_FALSE );

  LOG(("sbRemotePlayer::GetUserApprovalForHost(URI) - there"));

  // now we can actually go for the prompt
  nsCOMPtr<nsIPromptService> promptService =
    do_GetService( "@mozilla.org/embedcomp/prompt-service;1", &rv );
  NS_ENSURE_SUCCESS( rv, PR_FALSE );

  // this is launched by the child window, tell Gecko about it.
  nsCOMPtr<nsPIDOMWindow> jsWindow = GetWindowFromJS();
  nsCOMPtr<nsIDOMWindow> window = do_QueryInterface( jsWindow, &rv );
  NS_ENSURE_SUCCESS( rv, PR_FALSE );

  PRUint32 buttons;
  if (aScopedName) {
    // Used for all the security prompts
    buttons = nsIPromptService::BUTTON_POS_0 * nsIPromptService::BUTTON_TITLE_IS_STRING + /* allow for visit */
              nsIPromptService::BUTTON_POS_1 * nsIPromptService::BUTTON_TITLE_IS_STRING + /* cancel */
              nsIPromptService::BUTTON_POS_2 * nsIPromptService::BUTTON_TITLE_IS_STRING + /* allow always */
              nsIPromptService::BUTTON_POS_1_DEFAULT;
  }
  else {
    // Used for playback control only
    buttons =  nsIPromptService::BUTTON_POS_0 * nsIPromptService::BUTTON_TITLE_YES +
               nsIPromptService::BUTTON_POS_1 * nsIPromptService::BUTTON_TITLE_NO;
  }
  LOG(("sbRemotePlayer::GetUserApprovalForHost(URI) - where"));

  PRInt32 allowed;
  // the buttons have to be in this weird order for the cancel button to be
  // properly enabled while we delay allowing the user to accept
  rv = promptService->ConfirmEx( window,
                                 title.BeginReading(),
                                 message.BeginReading(),
                                 buttons,
                                 allowOnce.BeginReading(),
                                 allowDeny.BeginReading(),
                                 allowAlways.BeginReading(),
                                 nsnull,
                                 nsnull,
                                 &allowed );
  NS_ENSURE_SUCCESS( rv, PR_FALSE );

  PRBool retval = PR_FALSE;
#ifdef METRICS_ENABLED
  nsString metricsCategory;
  metricsCategory.AssignLiteral("rapi.prompt.");
  switch(allowed) {
    case 2:
      // set the permissions permanently
      metricsCategory.AppendLiteral("always");
      rv = sbSecurityMixin::SetPermission( aURI, nsDependentCString(aScopedName) );
      retval = PR_TRUE;
      break;
    case 0:
      // Allow once
      metricsCategory.AppendLiteral("once");
      retval = PR_TRUE;
      break;
    case 1:
      // Disallow
      metricsCategory.AppendLiteral("deny");
      break;
  }

  if (aScopedName) {
    nsCOMPtr<sbIMetrics> metrics =
      do_CreateInstance("@songbirdnest.com/Songbird/Metrics;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCString name(aScopedName);
    rv = metrics->MetricsInc(metricsCategory,
                             NS_ConvertASCIItoUTF16(name),
                             EmptyString());
    NS_ENSURE_SUCCESS(rv, rv);
  }
#endif // METRICS_ENABLED

  return retval;
}

nsresult
sbRemotePlayer::GetBrowser( nsIDOMElement** aElement )
{
  nsresult rv;
  // Find the sb-tabbrowser ByTagName()[0] (not ById()!)
  NS_ENSURE_STATE(mChromeDoc);
  nsCOMPtr<nsIDOMNodeList> tabBrowserElementList;
  mChromeDoc->GetElementsByTagName( SB_WEB_TABBROWSER,
                              getter_AddRefs(tabBrowserElementList) );
  NS_ENSURE_STATE(tabBrowserElementList);
  nsCOMPtr<nsIDOMNode> tabBrowserElement;
  rv = tabBrowserElementList->Item( 0, getter_AddRefs(tabBrowserElement) );
  NS_ENSURE_STATE(tabBrowserElement);
  NS_ENSURE_SUCCESS( rv, rv );

  // Get our interface
  nsCOMPtr<sbITabBrowser> tabbrowser( do_QueryInterface( tabBrowserElement,
                                                         &rv ) );
  NS_ENSURE_SUCCESS( rv, rv );

  rv = tabbrowser->GetBrowserForDocument( mContentDoc, aElement );
  NS_ENSURE_SUCCESS( rv, rv );

  return NS_OK;
}

nsresult
sbRemotePlayer::TakePlaybackControl( nsIURI* aURI )
{
  nsresult rv;
  nsCOMPtr<nsIURI> uri = aURI;

  // try to guess the URI if we are not given one
  if ( NS_UNLIKELY(!uri) ) {
    nsCOMPtr<sbISecurityMixin> securityMixin =
      do_QueryInterface( mSecurityMixin, &rv );
    NS_ENSURE_SUCCESS( rv, rv );
    rv = securityMixin->GetCodebase( getter_AddRefs(uri) );
    NS_ENSURE_SUCCESS( rv, rv );
  }

  nsCOMPtr<sbIRemoteAPIService> remoteAPIService =
    do_GetService( "@songbirdnest.com/remoteapi/remoteapiservice;1", &rv );
  NS_ENSURE_SUCCESS( rv, rv );

  rv = remoteAPIService->TakePlaybackControl( uri, nsnull );
  NS_ENSURE_SUCCESS( rv, rv );
  return NS_OK;
}


// ---------------------------------------------------------------------------
//
//                             helpers
//
// ---------------------------------------------------------------------------

/* static */ already_AddRefed<nsPIDOMWindow>
sbRemotePlayer::GetWindowFromJS()
{
  LOG(("sbRemotePlayer::GetWindowFromJS()"));
  // Get the js context from the top of the stack. We may need to iterate
  // over these at some point in order to make sure we are sending events
  // to the correct window, but for now this works.
  nsCOMPtr<nsIJSContextStack> stack =
                           do_GetService("@mozilla.org/js/xpc/ContextStack;1");

  if (!stack) {
    LOG(("sbRemotePlayer::GetWindowFromJS() -- NO STACK!!!"));
    return nsnull;
  }

  JSContext *cx;
  if (NS_FAILED(stack->Peek(&cx)) || !cx) {
    LOG(("sbRemotePlayer::GetWindowFromJS() -- NO CONTEXT!!!"));
    return nsnull;
  }

  // Get the script context from the js context
  nsCOMPtr<nsIScriptContext> scCx = GetScriptContextFromJSContext(cx);
  NS_ENSURE_TRUE(scCx, nsnull);

  // Get the DOMWindow from the script global via the script context
  nsCOMPtr<nsPIDOMWindow> win = do_QueryInterface( scCx->GetGlobalObject() );
  NS_ENSURE_TRUE( win, nsnull );
  NS_ADDREF( win.get() );
  return win.get();
}

already_AddRefed<nsPIDOMWindow>
sbRemotePlayer::GetWindow()
{
  LOG(("sbRemotePlayer::GetWindow()"));
  NS_ASSERTION(mPrivWindow, "mPrivWindow is null");

  nsPIDOMWindow* window = mPrivWindow;
  NS_ADDREF(window);
  return window;
}

nsresult
sbRemotePlayer::DispatchEvent( nsIDOMDocument *aDoc,
                               const nsAString &aClass,
                               const nsAString &aType,
                               PRBool aIsTrusted )
{
  LOG(( "sbRemotePlayer::DispatchEvent(%s, %s)",
        NS_LossyConvertUTF16toASCII(aClass).get(),
        NS_LossyConvertUTF16toASCII(aType).get() ));

  //change interfaces to create the event
  nsresult rv;
  nsCOMPtr<nsIDOMDocumentEvent> docEvent( do_QueryInterface( aDoc, &rv ) );
  NS_ENSURE_SUCCESS( rv , rv );

  //create the event
  nsCOMPtr<nsIDOMEvent> event;
  docEvent->CreateEvent( aClass, getter_AddRefs(event) );
  NS_ENSURE_STATE(event);
  rv = event->InitEvent( aType, PR_TRUE, PR_TRUE );
  NS_ENSURE_SUCCESS( rv , rv );

  //use the document for a target.
  nsCOMPtr<nsIDOMEventTarget> eventTarget( do_QueryInterface( aDoc, &rv ) );
  NS_ENSURE_SUCCESS( rv, rv );

  //make the event trusted
  nsCOMPtr<nsIPrivateDOMEvent> privEvt( do_QueryInterface( event, &rv ) );
  NS_ENSURE_SUCCESS( rv, rv );
  privEvt->SetTrusted(aIsTrusted);

  // Fire an event to the chrome system. This event will NOT get to content.
  PRBool dummy;
  return eventTarget->DispatchEvent( event, &dummy );
}

nsresult
sbRemotePlayer::DispatchSecurityEvent( nsIDOMDocument *aDoc,
                                       sbIRemotePlayer *aPlayer,
                                       const nsAString &aClass,
                                       const nsAString &aType,
                                       const nsAString &aCategoryID,
                                       PRBool aHasAccess,
                                       PRBool aIsTrusted )
{
  LOG(( "sbRemotePlayer::DispatchSecurityEvent(%s, %s)",
    NS_LossyConvertUTF16toASCII(aClass).get(),
    NS_LossyConvertUTF16toASCII(aType).get() ));

  NS_ENSURE_ARG_POINTER(aDoc);
  NS_ENSURE_ARG_POINTER(aPlayer);

  nsresult rv;

  //change interfaces to create the event
  nsCOMPtr<nsIDOMDocumentEvent>
    docEvent( do_QueryInterface( aDoc, &rv ) );
  NS_ENSURE_SUCCESS( rv , rv );

  //create the event
  nsCOMPtr<nsIDOMEvent> event;
  docEvent->CreateEvent( aClass, getter_AddRefs(event) );
  NS_ENSURE_STATE(event);
  rv = event->InitEvent( aType, PR_TRUE, PR_TRUE );
  NS_ENSURE_SUCCESS( rv , rv );

  //use the document for a target.
  nsCOMPtr<nsIDOMEventTarget>
    eventTarget( do_QueryInterface( aDoc, &rv ) );
  NS_ENSURE_SUCCESS( rv, rv );

  //make the event untrusted
  nsCOMPtr<nsIPrivateDOMEvent> privEvt( do_QueryInterface( event, &rv ) );
  NS_ENSURE_SUCCESS( rv, rv );
  privEvt->SetTrusted(aIsTrusted);

  // make our custom media item wrapper event
  nsRefPtr<sbRemoteSecurityEvent> securityEvent( new sbRemoteSecurityEvent() );
  securityEvent->Init();

  nsAutoString category;
  sbRemotePlayer::GetJSScopeNameFromScope( NS_LossyConvertUTF16toASCII( aCategoryID ), category );

  nsCOMPtr<nsIURI> scopeURI;
  rv = aPlayer->GetSiteScope( getter_AddRefs( scopeURI ) );
  NS_ENSURE_SUCCESS( rv, rv );

  rv = securityEvent->InitEvent( event, scopeURI, category, aCategoryID, aHasAccess );
  NS_ENSURE_SUCCESS( rv, rv );

  // Fire an event to the chrome system.
  PRBool dummy;
  return eventTarget->DispatchEvent( securityEvent, &dummy );
}

PRBool
sbRemotePlayer::IsPrivileged()
{
  return mPrivileged;
}

/*static*/ nsresult
sbRemotePlayer::GetJSScopeNameFromScope( const nsACString &aScopeName,
                                         nsAString &aJSScopeName )

{
  for ( PRUint32 i=0; i < NS_ARRAY_LENGTH(sPublicCategoryConversions); i++ ) {
    if ( StringBeginsWith( nsDependentCString(sPublicCategoryConversions[i][1]), aScopeName ) ) {
      aJSScopeName.Assign( NS_ConvertASCIItoUTF16( sPublicCategoryConversions[i][0] ) );
      return NS_OK;
    }
  }

  return NS_ERROR_INVALID_ARG;
}

nsresult
sbRemotePlayer::InitRemoteWebPlaylist()
{
  nsresult rv;
  LOG(("sbRemotePlayer::InitRemoteWebPlaylist()"));

  // These get set in initialization, so if they aren't set, bad news
  if (!mChromeDoc || !mContentDoc)
    return NS_ERROR_FAILURE;

  // Find the sb-tabbrowser ByTagName()[0] (not ById()!)
  nsCOMPtr<nsIDOMNodeList> tabBrowserElementList;
  mChromeDoc->GetElementsByTagName( SB_WEB_TABBROWSER,
                              getter_AddRefs(tabBrowserElementList) );
  NS_ENSURE_STATE(tabBrowserElementList);
  nsCOMPtr<nsIDOMNode> tabBrowserElement;
  rv = tabBrowserElementList->Item( 0, getter_AddRefs(tabBrowserElement) );
  NS_ENSURE_STATE(tabBrowserElement);
  NS_ENSURE_SUCCESS( rv, rv );

  // Get our interface
  nsCOMPtr<sbITabBrowser> tabbrowser( do_QueryInterface( tabBrowserElement,
                                                         &rv ) );
  NS_ENSURE_SUCCESS( rv, rv );

  // Get the tab for our particular document
  nsCOMPtr<sbITabBrowserTab> browserTab;
  tabbrowser->GetTabForDocument( mContentDoc, getter_AddRefs(browserTab) );
  NS_ENSURE_STATE(browserTab);

  // Get the outer playlist from the tab, it's the web playlist
  nsCOMPtr<nsIDOMElement> playlist;
  browserTab->GetPlaylist( getter_AddRefs(playlist) );
  NS_ENSURE_STATE(playlist);

  nsCOMPtr<sbIPlaylistWidget> playlistWidget ( do_QueryInterface( playlist,
                                                                  &rv) );
  NS_ENSURE_SUCCESS( rv, rv );

  // Create the RemotePlaylistWidget
  nsRefPtr<sbRemoteWebPlaylist> pWebPlaylist =
                         new sbRemoteWebPlaylist( this, playlistWidget, browserTab );
  NS_ENSURE_TRUE( pWebPlaylist, NS_ERROR_FAILURE );

  rv = pWebPlaylist->Init();
  NS_ENSURE_SUCCESS( rv, rv );

  mRemWebPlaylist = pWebPlaylist;
  NS_ENSURE_TRUE( mRemWebPlaylist, NS_ERROR_FAILURE );

  return NS_OK;
}

sbRemoteNotificationManager*
sbRemotePlayer::GetNotificationManager()
{
  return mNotificationMgr;
}

nsresult
sbRemotePlayer::GetSiteScopeURL( nsAString& aURL )
{
  if (!mSiteScopeURL.IsVoid()) {
    aURL.Assign(mSiteScopeURL);
    return NS_OK;
  }

  nsCOMPtr<nsIURI> scopeURI = GetSiteScopeURI();
  NS_ENSURE_TRUE( scopeURI, NS_ERROR_FAILURE );

  nsCString scopeSpec;
  nsresult rv = scopeURI->GetSpec(scopeSpec);
  NS_ENSURE_SUCCESS( rv, rv );

  mSiteScopeURL.Assign(NS_ConvertUTF8toUTF16(scopeSpec));
  aURL.Assign(mSiteScopeURL);
  return NS_OK;
}

already_AddRefed<nsIURI>
sbRemotePlayer::GetSiteScopeURI()
{
  if (mSiteScopeURI) {
    nsIURI* retval = mSiteScopeURI;
    NS_ADDREF(retval);
    return retval;
  }

  nsresult rv;

  // check if the site scope has been set before; if not, set it implicitly
  if ( mScopeDomain.IsVoid() || mScopePath.IsVoid() ) {
    rv = SetSiteScope( mScopeDomain, mScopePath );
    NS_ENSURE_SUCCESS( rv, nsnull);
  }

  // get the codebase URI
  nsCOMPtr<sbISecurityMixin> mixin = do_QueryInterface( mSecurityMixin, &rv );
  NS_ENSURE_SUCCESS( rv, nsnull );

  nsCOMPtr<nsIURI> codebaseURI;
  rv = mixin->GetCodebase( getter_AddRefs(codebaseURI) );
  NS_ENSURE_SUCCESS( rv, nsnull );

  // construct a download scope URI starting with the codebase URI scheme
  nsCString scopeSpec;
  rv = codebaseURI->GetScheme(scopeSpec);
  NS_ENSURE_SUCCESS( rv, nsnull );

  scopeSpec.AppendLiteral(":");

  nsCOMPtr<nsIURI> scopeURI;
  rv = mIOService->NewURI( scopeSpec, nsnull, nsnull, getter_AddRefs(scopeURI) );
  NS_ENSURE_SUCCESS( rv, nsnull );

  rv = scopeURI->SetHost(mScopeDomain);
  NS_ENSURE_SUCCESS( rv, nsnull );

  rv = scopeURI->SetPath(mScopePath);
  NS_ENSURE_SUCCESS( rv, nsnull );

  scopeURI = NS_TryToMakeImmutable( scopeURI, &rv );
  NS_ENSURE_SUCCESS( rv, nsnull );

  mSiteScopeURI = scopeURI;
  return scopeURI.forget();
}

nsresult
sbRemotePlayer::SetDownloadScope( sbIMediaItem *aItem,
                                  const nsAString& aSiteID )
{
  LOG(( "sbRemotePlayer::SetDownloadScope()" ));
  NS_ASSERTION(aItem, "aItem is null");
  NS_ASSERTION( !aSiteID.IsEmpty(), "aSiteID is empty!" );

  nsresult rv;

  // get the unwrapped media item
  nsCOMPtr<sbIMediaItem> mediaItem;
  nsCOMPtr<sbIWrappedMediaItem> wrappedMediaItem = do_QueryInterface( aItem,
                                                                      &rv );
  if (NS_SUCCEEDED(rv)) {
    mediaItem = wrappedMediaItem->GetMediaItem();
    NS_ENSURE_TRUE( mediaItem, NS_ERROR_FAILURE );
  } else {
    mediaItem = aItem;
  }

  nsString siteScopeURL;
  rv = GetSiteScopeURL(siteScopeURL);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mediaItem->SetProperty( NS_LITERAL_STRING(SB_PROPERTY_RAPISCOPEURL),
                               siteScopeURL );
  NS_ENSURE_SUCCESS( rv, rv );

  rv = mediaItem->SetProperty( NS_LITERAL_STRING(SB_PROPERTY_RAPISITEID),
                               aSiteID );
  NS_ENSURE_SUCCESS( rv, rv );

  return NS_OK;
}

nsresult
sbRemotePlayer::SetOriginScope( sbIMediaItem *aItem,
                                const nsAString& aSiteID )
{
  LOG(("sbRemotePlayer::SetOriginScope()"));
  NS_ASSERTION( aItem, "aItem is null" );
  NS_ASSERTION( !aSiteID.IsEmpty(), "aSiteID is empty!" );

  nsresult rv = SetDownloadScope( aItem, aSiteID );
  NS_ENSURE_SUCCESS( rv, rv );

  nsString scope;
  rv = aItem->GetProperty( NS_LITERAL_STRING(SB_PROPERTY_RAPISCOPEURL), scope);
  NS_ENSURE_SUCCESS( rv, rv );

  rv = aItem->SetProperty( NS_LITERAL_STRING(SB_PROPERTY_ORIGINPAGE), scope );
  NS_ENSURE_SUCCESS( rv, rv );

  return NS_OK;
}

// ---------------------------------------------------------------------------
//
//                             Component stuff
//
// ---------------------------------------------------------------------------

NS_METHOD
sbRemotePlayer::Register( nsIComponentManager* aCompMgr,
                          nsIFile* aPath,
                          const char *aLoaderStr,
                          const char *aType,
                          const nsModuleComponentInfo *aInfo )
{
  nsresult rv;

  nsCOMPtr<nsICategoryManager> catMan(
                                do_GetService(NS_CATEGORYMANAGER_CONTRACTID) );
  if (!catMan)
    return NS_ERROR_FAILURE;

  // allow ourself to be accessed as a js global in webpage
  rv = catMan->AddCategoryEntry( JAVASCRIPT_GLOBAL_PROPERTY_CATEGORY,
                                 "songbird",      /* use this name to access */
                                 SONGBIRD_REMOTEPLAYER_CONTRACTID,
                                 PR_TRUE,         /* persist */
                                 PR_TRUE,         /* replace existing */
                                 nsnull );

  return rv;
}


NS_METHOD
sbRemotePlayer::Unregister( nsIComponentManager* aCompMgr,
                            nsIFile* aPath,
                            const char *aLoaderStr,
                            const nsModuleComponentInfo *aInfo )
{
  nsresult rv;

  nsCOMPtr<nsICategoryManager> catMan(
                                do_GetService(NS_CATEGORYMANAGER_CONTRACTID) );
  if (!catMan)
    return NS_ERROR_FAILURE;

  rv = catMan->DeleteCategoryEntry( JAVASCRIPT_GLOBAL_PROPERTY_CATEGORY,
                                    "songbird",
                                    PR_TRUE );   /* delete persisted data */
  return rv;
}

nsresult
sbRemotePlayer::CreateProperty( const nsAString& aPropertyType,
                                const nsAString& aPropertyID,
                                const nsAString& aDisplayName,
                                const nsAString& aButtonLabel,
                                PRInt32 aTimeType,
                                PRBool aReadonly,
                                PRBool aUserViewable,
                                PRUint32 aNullSort )
{
#ifdef PR_LOGGING
  nsCString viewable = aUserViewable? NS_LITERAL_CSTRING("TRUE") : NS_LITERAL_CSTRING("FALSE");
  nsCString readonly = aReadonly? NS_LITERAL_CSTRING("TRUE") : NS_LITERAL_CSTRING("FALSE");
  LOG(( "sbRemotePlayer::CreateProperty(%s, %s, %s, %s, viewable:%s, :reado%s)",
        NS_LossyConvertUTF16toASCII(aPropertyType).get(),
        NS_LossyConvertUTF16toASCII(aPropertyID).get(),
        NS_LossyConvertUTF16toASCII(aDisplayName).get(),
        NS_LossyConvertUTF16toASCII(aButtonLabel).get(),
        viewable.get(),
        readonly.get() ));
#endif

  nsresult rv;
  nsCOMPtr<sbIPropertyManager> propMngr(
                         do_GetService( SB_PROPERTYMANAGER_CONTRACTID, &rv ) );
  NS_ENSURE_SUCCESS( rv, rv );

  PRBool hasProp;
  nsCOMPtr<sbIPropertyInfo> info;
  propMngr->HasProperty( aPropertyID, &hasProp );

  if (!hasProp) {
    // property doesn't exist, add it.

    // these types get created directly
    if (aPropertyType.EqualsLiteral("text") ||
        aPropertyType.EqualsLiteral("datetime") ||
        aPropertyType.EqualsLiteral("uri") ||
        aPropertyType.EqualsLiteral("number")) {
      if ( aPropertyType.EqualsLiteral("text") ){
        info = do_CreateInstance( SB_TEXTPROPERTYINFO_CONTRACTID, &rv );
      } else if ( aPropertyType.EqualsLiteral("datetime") ) {
        info = do_CreateInstance( SB_DATETIMEPROPERTYINFO_CONTRACTID, &rv );
        NS_ENSURE_SUCCESS( rv, rv );

        nsCOMPtr<sbIDatetimePropertyInfo> dtInfo( do_QueryInterface( info, &rv ) );
        NS_ENSURE_SUCCESS( rv, rv );

        rv = dtInfo->SetTimeType(aTimeType);
        NS_ENSURE_SUCCESS( rv, rv );

      } else if ( aPropertyType.EqualsLiteral("uri") ) {
        info = do_CreateInstance( SB_URIPROPERTYINFO_CONTRACTID, &rv );
      } else if ( aPropertyType.EqualsLiteral("number") ) {
        info = do_CreateInstance( SB_NUMBERPROPERTYINFO_CONTRACTID, &rv );
      }
      NS_ENSURE_SUCCESS( rv, rv );

      if (info) {
        // set the data on the propinfo
        rv = info->SetId(aPropertyID);
        NS_ENSURE_SUCCESS( rv, rv );
        rv = info->SetDisplayName(aDisplayName);
        NS_ENSURE_SUCCESS( rv, rv );
        rv = info->SetUserViewable(aUserViewable);
        NS_ENSURE_SUCCESS( rv, rv );
        // aReadonly maps inversely to UserEditable
        rv = info->SetUserEditable(aReadonly ? PR_FALSE : PR_TRUE);
        NS_ENSURE_SUCCESS( rv, rv );
        rv = info->SetNullSort(aNullSort);
        NS_ENSURE_SUCCESS( rv, rv );
      }
    }
    // these types get created through builders
    else {
      nsCOMPtr<sbIPropertyBuilder> builder;
      if (aPropertyType.EqualsLiteral("button")) {
        LOG(( "sbRemotePlayer::CreateProperty() - using a button builder" ));
        builder =
          do_CreateInstance(SB_SIMPLEBUTTONPROPERTYBUILDER_CONTRACTID, &rv);
        NS_ENSURE_SUCCESS(rv, rv);

        nsCOMPtr<sbISimpleButtonPropertyBuilder> buttonBuilder (
          do_QueryInterface(builder));
        NS_ENSURE_STATE(buttonBuilder);

        rv = buttonBuilder->SetLabel(aButtonLabel);
        NS_ENSURE_SUCCESS(rv, rv);
      }
      else if (aPropertyType.EqualsLiteral("image")) {
        LOG(( "sbRemotePlayer::CreateProperty() - using a image builder" ));
        builder =
          do_CreateInstance(SB_IMAGEPROPERTYBUILDER_CONTRACTID, &rv);
        NS_ENSURE_SUCCESS(rv, rv);
      }
      else if (aPropertyType.EqualsLiteral("downloadbutton")) {
        LOG(( "sbRemotePlayer::CreateProperty() - using a downloadbutton builder" ));
        builder =
          do_CreateInstance(SB_DOWNLOADBUTTONPROPERTYBUILDER_CONTRACTID, &rv);
        NS_ENSURE_SUCCESS(rv, rv);

        nsCOMPtr<sbIDownloadButtonPropertyBuilder> dlBuilder (
          do_QueryInterface(builder));
        NS_ENSURE_STATE(dlBuilder);

        rv = dlBuilder->SetLabel(aButtonLabel);
        NS_ENSURE_SUCCESS(rv, rv);
      }
      else if (aPropertyType.EqualsLiteral("rating")) {
        LOG(( "sbRemotePlayer::CreateProperty() - using a rating builder" ));
        builder =
          do_CreateInstance(SB_RATINGPROPERTYBUILDER_CONTRACTID, &rv);
        NS_ENSURE_SUCCESS(rv, rv);
      }
      else {
        // bad type, return an error - Should we just create text by default?
        return NS_ERROR_FAILURE;
      }

      // set all the common properties together
      if (builder) {
        rv = builder->SetPropertyID(aPropertyID);
        NS_ENSURE_SUCCESS(rv, rv);
        rv = builder->SetDisplayName(aDisplayName);
        NS_ENSURE_SUCCESS(rv, rv);
        rv = builder->SetRemoteReadable(PR_TRUE);
        NS_ENSURE_SUCCESS(rv, rv);
        rv = builder->SetRemoteWritable(PR_TRUE);
        NS_ENSURE_SUCCESS(rv, rv);
        rv = builder->SetUserViewable(aUserViewable);
        NS_ENSURE_SUCCESS( rv, rv );
        rv = builder->SetUserEditable(aReadonly ? PR_FALSE : PR_TRUE);
        NS_ENSURE_SUCCESS( rv, rv );
        rv = builder->Get(getter_AddRefs(info));
        NS_ENSURE_SUCCESS(rv, rv);
      }

    }

    // we created a new property in the system so enable remote write/read
    if (NS_SUCCEEDED(rv) ) {
      LOG(( "sbRemotePlayer::CreateProperty() - we have a propertyInfo item" ));
      rv = info->SetRemoteWritable(PR_TRUE);
      if ( NS_FAILED(rv) && rv != NS_ERROR_NOT_IMPLEMENTED ) {
        return rv;
      }

      rv = info->SetRemoteReadable(PR_TRUE);
      if ( NS_FAILED(rv) && rv != NS_ERROR_NOT_IMPLEMENTED ) {
        return rv;
      }
    } else {
      LOG(( "sbRemotePlayer::CreateProperty() - we DONT have a propertyInfo item" ));
    }

    // add to the property manager
    rv = propMngr->AddPropertyInfo(info);
    NS_ENSURE_SUCCESS( rv, rv );

  }

  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayer::CreateTextProperty( const nsAString& aPropertyID,
                                    const nsAString& aDisplayName,
                                    PRBool aReadonly,
                                    PRBool aUserViewable,
                                    PRUint32 aNullSort )
{
  return CreateProperty( NS_LITERAL_STRING("text"),
                         aPropertyID,
                         aDisplayName,
                         EmptyString(),
                         0,
                         aReadonly,
                         aUserViewable,
                         aNullSort );
}

NS_IMETHODIMP
sbRemotePlayer::CreateDateTimeProperty( const nsAString& aPropertyID,
                                        const nsAString& aDisplayName,
                                        PRInt32 aTimeType,
                                        PRBool aReadonly,
                                        PRBool aUserViewable,
                                        PRUint32 aNullSort )
{
  return CreateProperty( NS_LITERAL_STRING("datetime"),
                         aPropertyID,
                         aDisplayName,
                         EmptyString(),
                         aTimeType,
                         aReadonly,
                         aUserViewable,
                         aNullSort );
}

NS_IMETHODIMP
sbRemotePlayer::CreateURIProperty( const nsAString& aPropertyID,
                                   const nsAString& aDisplayName,
                                   PRBool aReadonly,
                                   PRBool aUserViewable,
                                   PRUint32 aNullSort)
{
  return CreateProperty( NS_LITERAL_STRING("uri"),
                         aPropertyID,
                         aDisplayName,
                         EmptyString(),
                         0,
                         aReadonly,
                         aUserViewable,
                         aNullSort );
}

NS_IMETHODIMP
sbRemotePlayer::CreateNumberProperty( const nsAString& aPropertyID,
                                      const nsAString& aDisplayName,
                                      PRBool aReadonly,
                                      PRBool aUserViewable,
                                      PRUint32 aNullSort )
{
  return CreateProperty( NS_LITERAL_STRING("number"),
                         aPropertyID,
                         aDisplayName,
                         EmptyString(),
                         0,
                         aReadonly,
                         aUserViewable,
                         aNullSort );
}

NS_IMETHODIMP
sbRemotePlayer::CreateImageProperty( const nsAString& aPropertyID,
                                     const nsAString& aDisplayName,
                                     PRBool aReadonly,
                                     PRBool aUserViewable,
                                     PRUint32 aNullSort )
{
  return CreateProperty( NS_LITERAL_STRING("image"),
                         aPropertyID,
                         aDisplayName,
                         EmptyString(),
                         0,
                         aReadonly,
                         aUserViewable,
                         aNullSort );
}

NS_IMETHODIMP
sbRemotePlayer::CreateRatingsProperty( const nsAString& aPropertyID,
                                       const nsAString& aDisplayName,
                                       PRBool aReadonly,
                                       PRBool aUserViewable,
                                       PRUint32 aNullSort )
{
  return CreateProperty( NS_LITERAL_STRING("rating"),
                         aPropertyID,
                         aDisplayName,
                         EmptyString(),
                         0,
                         aReadonly,
                         aUserViewable,
                         aNullSort );
}

NS_IMETHODIMP
sbRemotePlayer::CreateButtonProperty( const nsAString& aPropertyID,
                                      const nsAString& aDisplayName,
                                      const nsAString& aButtonLabel,
                                      PRBool aReadonly,
                                      PRBool aUserViewable,
                                      PRUint32 aNullSort )
{
  return CreateProperty( NS_LITERAL_STRING("button"),
                         aPropertyID,
                         aDisplayName,
                         aButtonLabel,
                         0,
                         aReadonly,
                         aUserViewable,
                         aNullSort );
}

NS_IMETHODIMP
sbRemotePlayer::CreateDownloadButtonProperty( const nsAString& aPropertyID,
                                              const nsAString& aDisplayName,
                                              const nsAString& aButtonLabel,
                                              PRBool aReadonly,
                                              PRBool aUserViewable,
                                              PRUint32 aNullSort )
{
  return CreateProperty( NS_LITERAL_STRING("downloadbutton"),
                         aPropertyID,
                         aDisplayName,
                         aButtonLabel,
                         0,
                         aReadonly,
                         aUserViewable,
                         aNullSort );
}

NS_IMETHODIMP
sbRemotePlayer::HasAccess( const nsAString& aRemotePermCategory,
                           PRBool *_retval )
{
  nsCOMPtr<sbISecurityMixin> mixin;
  mixin = do_QueryInterface(mSecurityMixin);

  // Here we are going to look up the aRemotePermCategory in our
  // sPublicCategoryConversions table to convert the more readable version
  // into our internal version.
  PRInt32 iIndex = -1;
  for ( PRUint32 i=0; iIndex < 0 && i < NS_ARRAY_LENGTH(sPublicCategoryConversions); i++ ) {
    if ( StringBeginsWith( aRemotePermCategory, NS_ConvertASCIItoUTF16( sPublicCategoryConversions[i][0] ) ) ) {
      // Set iIndex which will drop us out of the loop
      iIndex = i;
    }
  }

  // Check that we found a matching catagory, and return false if not.
  if (iIndex == -1)
  {
    *_retval = false;
    return NS_OK;
  }

  // Now grab the internal category name and get the permissions
  nsString mCategory;
  mCategory.AssignLiteral(sPublicCategoryConversions[iIndex][1]);
  return mixin->GetPermissionForScopedNameWrapper( mCategory, _retval );
}

// ---------------------------------------------------------------------------
//
//                             sbRemotePlayerDownloadCallback
//
// ---------------------------------------------------------------------------

NS_IMPL_ISUPPORTS1(sbRemotePlayerDownloadCallback, sbIDeviceBaseCallback)

sbRemotePlayerDownloadCallback::sbRemotePlayerDownloadCallback()
{
  LOG(("sbRemotePlayerDownloadCallback::sbRemotePlayerDownloadCallback()"));
}

sbRemotePlayerDownloadCallback::~sbRemotePlayerDownloadCallback()
{
  LOG(("sbRemotePlayerDownloadCallback::~sbRemotePlayerDownloadCallback()"));
}

nsresult
sbRemotePlayerDownloadCallback::Initialize(sbRemotePlayer* aRemotePlayer)
{
  LOG(("sbRemotePlayerDownloadCallback::Initialize()"));
  NS_ASSERTION(aRemotePlayer, "aRemotePlayer is null");

  nsresult rv;

  // Get a weak reference to the remote player
  mWeakRemotePlayer = do_GetWeakReference
                    ( NS_ISUPPORTS_CAST(sbIRemotePlayer*, aRemotePlayer), &rv );
  NS_ENSURE_SUCCESS(rv, rv);

  // Get the codebase
  nsCOMPtr<sbISecurityMixin> mixin =
                        do_QueryInterface( aRemotePlayer->mSecurityMixin, &rv );
  NS_ENSURE_SUCCESS( rv, rv );
  rv = mixin->GetCodebase( getter_AddRefs(mCodebaseURI) );

  // Get the IO service
  mIOService = do_GetService("@mozilla.org/network/io-service;1", &rv);

  // Set up download callbacks
  nsCOMPtr<sbIDeviceManager> deviceManager;
  PRBool hasDeviceForCategory;

  deviceManager = do_GetService("@songbirdnest.com/Songbird/DeviceManager;1",
                                &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = deviceManager->HasDeviceForCategory
        (NS_LITERAL_STRING("Songbird Download Device"), &hasDeviceForCategory);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(hasDeviceForCategory, NS_ERROR_UNEXPECTED);
  rv = deviceManager->GetDeviceByCategory
                                (NS_LITERAL_STRING("Songbird Download Device"),
                                 getter_AddRefs(mDownloadDevice));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mDownloadDevice->AddCallback(this);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

void
sbRemotePlayerDownloadCallback::Finalize()
{
  LOG(("sbRemotePlayerDownloadCallback::Finalize()"));

  if (mDownloadDevice)
    mDownloadDevice->RemoveCallback(this);
}

// ---------------------------------------------------------------------------
//
//                           sbIDeviceBaseCallback
//
// ---------------------------------------------------------------------------

NS_IMETHODIMP
sbRemotePlayerDownloadCallback::OnDeviceConnect
                                          ( const nsAString &aDeviceIdentifier )
{
  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayerDownloadCallback::OnDeviceDisconnect
                                          ( const nsAString &aDeviceIdentifier )
{
  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayerDownloadCallback::OnTransferStart( sbIMediaItem* aMediaItem )
{
  LOG(("sbRemotePlayer::OnTransferStart()"));
  NS_ASSERTION(aMediaItem, "aMediaItem is null");
  nsresult rv;

  // Check item scope.  Return without error if out of scope
  rv = CheckItemScope( aMediaItem );
  if (NS_FAILED(rv))
    return NS_OK;

  nsCOMPtr<sbIRemotePlayer> remotePlayer =
                                      do_QueryReferent(mWeakRemotePlayer, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  if (remotePlayer) {
    rv = remotePlayer->FireMediaItemStatusEventToContent
                                                ( RAPI_EVENT_CLASS,
                                                  RAPI_EVENT_TYPE_DOWNLOADSTART,
                                                  aMediaItem,
                                                  NS_OK );
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayerDownloadCallback::OnTransferComplete( sbIMediaItem* aMediaItem,
                                                    PRInt32 aStatus )
{
  LOG(("sbRemotePlayer::OnTransferComplete()"));
  NS_ASSERTION(aMediaItem, "aMediaItem is null");
  nsresult rv;

  // Check item scope.  Return without error if out of scope
  rv = CheckItemScope( aMediaItem );
  if (NS_FAILED(rv))
    return NS_OK;

  nsCOMPtr<sbIRemotePlayer> remotePlayer =
                                      do_QueryReferent(mWeakRemotePlayer, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  if (remotePlayer) {
    rv = remotePlayer->FireMediaItemStatusEventToContent
                                            ( RAPI_EVENT_CLASS,
                                              RAPI_EVENT_TYPE_DOWNLOADCOMPLETE,
                                              aMediaItem,
                                              aStatus );
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

NS_IMETHODIMP
sbRemotePlayerDownloadCallback::OnStateChanged
                                          ( const nsAString &aDeviceIdentifier,
                                            PRUint32 aState )
{
  return NS_OK;
}

nsresult
sbRemotePlayerDownloadCallback::CheckItemScope( sbIMediaItem* aMediaItem )
{
  LOG(("sbRemotePlayer::CheckItemScope()"));
  nsresult rv;

  // Get the item scope
  nsCAutoString scopeDomain;
  nsCAutoString scopePath;
  rv = GetItemScope( aMediaItem, scopeDomain, scopePath );
  NS_ENSURE_SUCCESS(rv, rv);

  // Check the item scope against the codebase
  rv = sbURIChecker::CheckURI( scopeDomain, scopePath, mCodebaseURI );

  return rv;
}

nsresult
sbRemotePlayerDownloadCallback::GetItemScope( sbIMediaItem* aMediaItem,
                                              nsACString& aScopeDomain,
                                              nsACString& aScopePath )
{
  LOG(("sbRemotePlayer::GetItemScope()"));
  NS_ASSERTION(aMediaItem, "aMediaItem is null");
  nsresult rv;

  // Get the scope property.  If not available, use the origin page property
  nsAutoString scopeSpec;
  rv = aMediaItem->GetProperty( NS_LITERAL_STRING(SB_PROPERTY_RAPISCOPEURL),
                                scopeSpec );
  if (NS_FAILED(rv) || scopeSpec.IsEmpty()) {
    rv = aMediaItem->GetProperty( NS_LITERAL_STRING(SB_PROPERTY_ORIGINPAGE),
                                  scopeSpec );
    // It is possible the download was not triggered by us so ignore the notification
    if (NS_FAILED(rv) || scopeSpec.IsEmpty())
      return rv;
  }

  // Get the scope URI
  nsCOMPtr<nsIURI> scopeURI;
  rv = mIOService->NewURI( NS_ConvertUTF16toUTF8(scopeSpec),
                           nsnull,
                           nsnull,
                           getter_AddRefs(scopeURI) );
  NS_ENSURE_SUCCESS(rv, rv);

  // Get the scope domain and path
  rv = sbURIChecker::CheckURI(aScopeDomain, aScopePath, scopeURI);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}
