Building out-of-tree
====================

After ``west init -l . && west update`` in the repo root:

Mouse (debug)::

   west build -p auto -b hitscan_nrf52840 applications/gaming_mouse \
       --sysbuild -- -DCONF_FILE=prj_esb.conf

Mouse (release)::

   west build -p auto -b hitscan_nrf52840 applications/gaming_mouse \
       --sysbuild -- -DCONF_FILE=prj_esb_release.conf

Low-speed dongle (debug)::

   west build -p auto -b hitscan52820_nrf52820 applications/gaming_mouse \
       --sysbuild -- -DCONF_FILE=prj.conf

Low-speed dongle (release)::

   west build -p auto -b hitscan52820_nrf52820 applications/gaming_mouse \
       --sysbuild -- -DCONF_FILE=prj_release.conf

Development DK target (for verifying the build system without the customer board)::

   west build -p auto -b nrf52833dk_nrf52833 applications/gaming_mouse --sysbuild
