Metadata Updates
================

When a [MediaItem](@ref sbIMediaItem) gets played, it's metadata may change because the playback
core is offering new metadata about this [MediaItem](@ref sbIMediaItem) that wasn't available when
metadata was first read from it because it was added to a Library.

Following is a description of all cases where metadata may be overwritten
automatically.

 1. Artist/Album/Genre will be overwritten only if there is no value in the
    database.

 2. Title will be overwritten only if there is no value or if the value is equal
    to what it calculates the "default" value should be based on the url.

    Because we set the "default" value upon insertion via the page scraper, or media
    import, we want to make sure to overwrite it if there's more complete metadata now
    available.

 3. Length will always be overwritten if the core returns a "real" length.  We
    assume that the core knows better than we do, though that is not necessarily
    the case with some VBR files where the length calculation becomes progressively
    more correct over time but can be wildly incorrect in the beginning on some
    [MediaItems](@ref sbIMediaItem).

If you want to ensure that your metadata is always displayed, you will have to
create alternate properties for your metadata and add columns for these
properties. After doing so, you may hide the default columns so only your
metadata is displayed.

