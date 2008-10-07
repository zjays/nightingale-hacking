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

#ifndef __SBSTRINGUTILS_H__
#define __SBSTRINGUTILS_H__

#include <nsStringGlue.h>
#include <nsTArray.h>
#include <prprf.h>

class nsIStringEnumerator;

/**
 * Class used to create strings from other data types.
 */
class sbAutoString : public nsAutoString
{
public:
  sbAutoString(int aValue)
  {
    char valueStr[64];

    PR_snprintf(valueStr, sizeof(valueStr), "%d", aValue);
    AssignLiteral(valueStr);
  }

  sbAutoString(PRUint32 aValue)
  {
    char valueStr[64];

    PR_snprintf(valueStr, sizeof(valueStr), "%lu", aValue);
    AssignLiteral(valueStr);
  }

  sbAutoString(PRUint64 aValue)
  {
    char valueStr[64];

    PR_snprintf(valueStr, sizeof(valueStr), "%llu", aValue);
    AssignLiteral(valueStr);
  }
};

/// @see nsString::FindCharInSet
PRInt32 nsString_FindCharInSet(const nsAString& aString,
                               const char *aPattern,
                               PRInt32 aOffset = 0);

void AppendInt(nsAString& str, PRUint64 val);

PRUint64 ToInteger64(const nsAString& str, nsresult* rv = nsnull);

nsresult SB_StringEnumeratorEquals(nsIStringEnumerator* aLeft,
                                   nsIStringEnumerator* aRight,
                                   PRBool* _retval);

/**
 * Searches a string for any occurences of any character of a set and replace
 * them with a replacement character.  Modifies the string in-place.
 * @see nsString_internal::ReplaceChar
 */
void nsString_ReplaceChar(/* inout */ nsAString& aString,
                          const nsAString& aOldChars,
                          const PRUnichar aNewChar);

/**
 * Return true if the given string is possibly UTF8
 * (i.e. it errs on the side of returning true)
 *
 * Note that it assumes all 7-bit encodings are utf8, and doesn't check for
 * invalid characters (e.g. 0xFFFE, surrogates).  This is a weaker check than
 * the nsReadableUtils version.
 */
PRBool IsLikelyUTF8(const nsACString& aString);

/**
 * Split the string specified by aString into sub-strings using the delimiter
 * specified by aDelimiter and place the sub-strings in the array specified by
 * aStringArray.
 *
 * \param aString               String to split.
 * \param aDelimiter            Sub-string delimiter.
 * \param aSubStringArray       Array of sub-strings.
 */

void nsString_Split(const nsAString&    aString,
                    const nsAString&    aDelimiter,
                    nsTArray<nsString>& aSubStringArray);

#endif /* __SBSTRINGUTILS_H__ */

