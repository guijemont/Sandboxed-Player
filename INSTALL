Installation Instructions
*************************

Prerequisites
=============

 * GNU/Linux Operating system
 * setuid-sandbox http://code.google.com/p/setuid-sandbox/
 * GStreamer 0.10 with the plugins from at least base and bad as well as the
   demuxers and decoders needed by decodebin2 for the videos you want to decode

To avoid some bugs, you might need to apply the patches in the following bugs:
 - https://bugzilla.gnome.org/show_bug.cgi?id=654900 (else the pipeline might
   stall altogether for some files)
 - https://bugzilla.gnome.org/show_bug.cgi?id=675134 (without this, the shm
   area won't be unlinked, leaving you with a file in /dev/shm/ )

Installation
============

 * install the prerequisites, make sure sandboxme is setuid root
 * ./autogen.sh SANDBOXME_PATH=/path/to/sandboxme
 * make
 * make install

Usual ./configure options can also apply to ./autogen.sh,  you can run autogen
and then "configure --help" to see these options.

Example use
===========

 gst-launch-0.10 filesrc location=/path/to/video_file ! sandboxeddecodebin name=decoder ! autovideosink decoder. ! autoaudiosink
