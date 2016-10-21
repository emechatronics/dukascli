dukascli
========

Tool to decode [Dukascopy][1]'s `.bin` and `.bi5` files.

Features
--------
- fast and portable
- capable of guessing time and date offsets from filename

Build
-----

    $ autoreconf -fi
    $ ./configure
    $ make

Usage
-----

    $ dukasdec AUDUSD/2016/02/02/01h_ticks


  [1]: http://www.dukascopy.com/
