/* vim: set sw=2 :miv */
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

#include <sbIMediacoreSequencer.h>

#include <nsIMutableArray.h>
#include <nsIStringEnumerator.h>

#include <nsCOMPtr.h>
#include <nsHashKeys.h>
#include <nsTHashtable.h>
#include <prmon.h>

#include <sbIMediacoreSequenceGenerator.h>
#include <sbIMediaListView.h>

class sbMediacoreSequencer : public sbIMediacoreSequencer
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_SBIMEDIACORESEQUENCER

  sbMediacoreSequencer();

  nsresult Init();
  
  nsresult RecalculateSequence();

private:
  virtual ~sbMediacoreSequencer();

protected:
  PRMonitor *mMonitor;
  
  PRUint32                       mMode;
  nsCOMPtr<sbIMediaListView>     mView;
  nsCOMPtr<nsIMutableArray>      mSequence;

  nsCOMPtr<sbIMediacoreSequenceGenerator> mCustomGenerator;
  nsCOMPtr<sbIMediacoreSequenceGenerator> mShuffleGenerator;
};
