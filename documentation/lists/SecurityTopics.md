Security
========

Sites can check what security restrictions it has through the
[Songbird](@ref sbIRemotePlayer) javascript object using the
[Songbird::hasAccess()](@ref sbIRemotePlayer::hasAccess) function.
Before trying to access these areas of the [Songbird](@ref sbIRemotePlayer)
object a check should be done to verify that the current site has access to the
area required.

Security Categories:
+ `"Control Playback"` - Access to commands like play and pause,
+ `"Read Current"` - Access to read information like `currentArtist`.
+ `"Read Library"` - Access to reading library information.
+ `"Modify Library"` - Access to create new libraries and write library information.

Examples
--------
### Proper way

~~~{.js}
  ...
  /*
   * Grab the current playing artist
   * Returns "N/A" if unable to retrieve artist.
   */
  function getArtistName ()
  {
    var artistName = "N/A";

    if (songbird.hasAccess("Read Current"))
    {
      artistName = songbird.currentArtist;
    }
    return artistName;
  }

  document.write("Current Playing artist is: " + getArtistName() + "<br />");
  ...
~~~

### Bad way

~~~{.js}
  ...
  /*
   * Grab the current playing artist
   * Returns "N/A" if unable to retrieve artist.
   */
  function getArtistName ()
  {
    var artistName = "N/A";

    try
    {
      artistName = songbird.currentArtist;
      /*
       * If you do not have access to read songbird.currentArtist
       * Then an exception will be thrown and your function will fail
       */
    } catch (errException)
      /*
       *  JavaScript error: , line ####: uncaught exception: Permission denied
       *  to get property Songbird Remote Player.currentArtist
       */
       alert(errException);
    }
    return artistName;
  }

  document.write("Current Playing artist is: " + getArtistName() + "<br />");
  ...
~~~
