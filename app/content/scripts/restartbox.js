/*
//
// BEGIN SONGBIRD GPL
// 
// This file is part of the Songbird web player.
//
// Copyright� 2006 Pioneers of the Inevitable LLC
// http://songbirdnest.com
// 
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the �GPL�).
// 
// Software distributed under the License is distributed 
// on an �AS IS� basis, WITHOUT WARRANTY OF ANY KIND, either 
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
 
//
//  sbIRestartBox
//

function sbRestartBox( title, message )
{
  try
  {
    var restartbox_data = new Object();
    restartbox_data.title = title;
    restartbox_data.message = message;
    restartbox_data.playing = gPPS.getPlaying();
    restartbox_data.ret = 0;
    SBOpenModalDialog( "chrome://songbird/content/xul/restartbox.xul", "restartbox", "chrome,modal=yes, centerscreen", restartbox_data );
    var restartOnPlaybackEnd = new sbIDataRemote("restart.onplaybackend");
    switch (restartbox_data.ret)
    {
      case 0: // restart later
              restartOnPlaybackEnd.SetValue( false );
              break;
      case 1: // restart now
              restartOnPlaybackEnd.SetValue( false );
              restartApp(); // assumes the current document has this function, bad.
              break;
      case 2: // restart on end of playback
              restartOnPlaybackEnd.SetValue( true );
              break;
    }
  }
  catch ( err )
  {
    alert("sbRestartBox - " + err);
  }
}

