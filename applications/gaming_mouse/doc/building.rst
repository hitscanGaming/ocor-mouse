Building out-of-tree
====================

After ``west init -l . && west update`` in the repo root:

Mouse (debug)::

   west build -p auto -b hitscan/nrf52840 applications/gaming_mouse \
       --sysbuild -- -DCONF_FILE=prj_esb.conf \
                     -DBOARD_ROOT=$PWD/applications/gaming_mouse

Mouse (release)::

   west build -p auto -b hitscan/nrf52840 applications/gaming_mouse \
       --sysbuild -- -DCONF_FILE=prj_esb_release.conf \
                     -DBOARD_ROOT=$PWD/applications/gaming_mouse

Low-speed dongle (debug)::

   west build -p auto -b hitscan52820/nrf52820 applications/gaming_mouse \
       --sysbuild -- -DCONF_FILE=prj.conf \
                     -DBOARD_ROOT=$PWD/applications/gaming_mouse

Low-speed dongle (release)::

   west build -p auto -b hitscan52820/nrf52820 applications/gaming_mouse \
       --sysbuild -- -DCONF_FILE=prj_release.conf \
                     -DBOARD_ROOT=$PWD/applications/gaming_mouse

Development DK target (no BOARD_ROOT needed — stock board)::

   west build -p auto -b nrf52833dk/nrf52833 applications/gaming_mouse --sysbuild

Notes
-----

* Board syntax uses the slash form ``vendor/soc`` per Zephyr Hardware Model V2.
* ``BOARD_ROOT`` points at the directory CONTAINING the ``boards/`` subdir.
  Custom hitscan boards live at ``applications/gaming_mouse/boards/nordic/hitscan/``
  and ``.../hitscan52820/``.
* On Windows, if you see ``ctypes\__init__.py line 157: AttributeError: class must
  define a '_type_' attribute`` when running ``west``, your shell is mixing
  Anaconda Python with the NCS toolchain Python. Either:

  - Use the NCS Toolchain Manager's bundled bash terminal (``ncsmgr.exe`` →
    "Open terminal"), which provides a clean PYTHONPATH/PATH.
  - Or deactivate conda: ``conda deactivate`` before invoking west.
