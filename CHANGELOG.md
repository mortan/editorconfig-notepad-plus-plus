CHANGELOG
=========

Unreleased
----------

* Check the encoding of a file against `charset` when it is opened and offer to convert or reinterpret it
* Warn before saving when characters can't be stored in the `latin1` (ANSI) encoding
* Show the current encoding of Notepad++ in the settings dialog

Version 0.4.0 (2019-02-21)
--------------------------

* Offer x64 build of the plugin [#9](https://github.com/editorconfig/editorconfig-notepad-plus-plus/issues/9)
* Convert `end_of_line` before saving [#26](https://github.com/editorconfig/editorconfig-notepad-plus-plus/pull/26)
* Add command to show current config settings [#23](https://github.com/editorconfig/editorconfig-notepad-plus-plus/pull/23)
* Keep folding state when saving the file [#21](https://github.com/editorconfig/editorconfig-notepad-plus-plus/pull/21)
* Fix property combination logic [#10](https://github.com/editorconfig/editorconfig-notepad-plus-plus/pull/10)
* Set syntax highlighting of the `.editorconfig` file to ini [#17](https://github.com/editorconfig/editorconfig-notepad-plus-plus/pull/17)
