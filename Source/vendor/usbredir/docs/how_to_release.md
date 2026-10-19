How to do a usbredir release
============================

Some notes to prepare a release, not really strict but better to have in order
to avoid forgetting something.

Preparing for the release
-------------------------

* Update the root `meson.build` and also the ones in usbredirparser and
  usbredirhost directories according to libtool rules.
* Update `ChangeLog.md` with list of changes done since last release
* Send a merge request with such changes, handle the review and merge it

Generate source tarball and tags
--------------------------------

* Tag the merged commit with release version `git tag -s -m "Release $version" $version`
* Be sure to be in a clean environment: `git clean -xfd`
* Create the source tarball with: `meson . build && meson dist -C build`
* Sign generated tarball: `cd build/meson-dist && gpg2 -sb usbredir-$version.tar.xz`
* If you have a Fedora account, you can proceed and check if
  a scratch-build works as expected.

Generate the MSI installer
--------------------------

* On the usbredir srcdir `mkdir build-win64 && cd build-win64`
* `mingw64-meson`
* `DESTDIR=./install-root ninja install`
* `DESTDIR=./install-root ninja data/usbredirect-x64-$version.msi`
* The MSI installer is then located at `build-win64/data`

Upload and update Info
----------------------

* Upload tarball and relative signature to
  `https://www.spice-space.org/download/usbredir/` and the MSI installer to
  `https://www.spice-space.org/download/windows/usbredirect/` with sftp
  `spice-uploader@spice-web.osci.io:/var/www/www.spice-space.org/download/`
* Push the tag to Gitlab `git push origin HEAD:main --tags`
* On Gitlab update tags (https://gitlab.freedesktop.org/spice/usbredir/-/tags)
  * Add ChangeLog information
  * Upload tarball with the signature
  * Upload the MSI installer
* Update file `download.rst` in
  https://gitlab.freedesktop.org/spice/spice-space-pages
* Create a merge request for `spice-space-pages`
