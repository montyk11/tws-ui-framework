SYSROOT=/home/rnd/STM32MP157F-DK2_Starter/sysroots/cortexa7t2hf-neon-vfpv4-ostl-linux-gnueabi
BOARD=root@192.168.7.1

# copy Widgets (explicit file)
scp "$SYSROOT/usr/lib/libQt6Widgets.so.6.6.3" $BOARD:/usr/lib/ && \
ssh $BOARD 'ln -sf /usr/lib/libQt6Widgets.so.6.6.3 /usr/lib/libQt6Widgets.so.6 && ln -sf /usr/lib/libQt6Widgets.so.6.6.3 /usr/lib/libQt6Widgets.so' || echo "Widgets scp failed"

# copy Core (explicit file) — adjust filename if ls from step 1 shows different version
scp "$SYSROOT/usr/lib/libQt6Core.so.6.6.3" $BOARD:/usr/lib/ && \
ssh $BOARD 'ln -sf /usr/lib/libQt6Core.so.6.6.3 /usr/lib/libQt6Core.so.6 && ln -sf /usr/lib/libQt6Core.so.6.6.3 /usr/lib/libQt6Core.so' || echo "Core scp failed"

# Copy the concrete files one-by-one so failures are visible:
scp "$SYSROOT/usr/lib/libicuuc.so.74.2"    $BOARD:/usr/lib/ || echo "scp libicuuc failed"
scp "$SYSROOT/usr/lib/libicudata.so.74.2"  $BOARD:/usr/lib/ || scp "$SYSROOT/usr/lib/libicudata.so.74.1" $BOARD:/usr/lib/ || echo "scp libicudata failed"
scp "$SYSROOT/usr/lib/libicui18n.so.74.2"  $BOARD:/usr/lib/ || scp "$SYSROOT/usr/lib/libicui18n.so.74.1" $BOARD:/usr/lib/ || echo "scp libicui18n failed"
scp "$SYSROOT/usr/lib/libicutu.so.74.2"    $BOARD:/usr/lib/ || echo "scp libicutu failed"

# Create soname symlinks on the board (run remotely)
ssh $BOARD 'ln -sf /usr/lib/libicuuc.so.74.2    /usr/lib/libicuuc.so.74 || true; \
            ln -sf /usr/lib/libicuuc.so.74.2    /usr/lib/libicuuc.so || true; \
            ln -sf /usr/lib/libicudata.so.74.2  /usr/lib/libicudata.so.74 || true; \
            ln -sf /usr/lib/libicudata.so.74.2  /usr/lib/libicudata.so || true; \
            ln -sf /usr/lib/libicui18n.so.74.2  /usr/lib/libicui18n.so.74 || true; \
            ln -sf /usr/lib/libicui18n.so.74.2  /usr/lib/libicui18n.so || true; \
            ln -sf /usr/lib/libicutu.so.74.2    /usr/lib/libicutu.so.74 || true; \
            ln -sf /usr/lib/libicutu.so.74.2    /usr/lib/libicutu.so || true; \
            echo "ICU symlinks created on target"; ls -l /usr/lib/libicu* || true'

# Copy libQt6DBus explicitly
scp "$SYSROOT/usr/lib/libQt6DBus.so.6.6.3" $BOARD:/usr/lib/

# Create symlinks on the board
ssh $BOARD 'ln -sf /usr/lib/libQt6DBus.so.6.6.3 /usr/lib/libQt6DBus.so.6; \
            ln -sf /usr/lib/libQt6DBus.so.6.6.3 /usr/lib/libQt6DBus.so; \
            ls -l /usr/lib/libQt6DBus*'
          
# Copy the exact GUI library and create the soname symlinks on the board
scp "$SYSROOT/usr/lib/libQt6Gui.so.6.6.3" $BOARD:/usr/lib/ \
  && ssh $BOARD 'ln -sf /usr/lib/libQt6Gui.so.6.6.3 /usr/lib/libQt6Gui.so.6; \
                 ln -sf /usr/lib/libQt6Gui.so.6.6.3 /usr/lib/libQt6Gui.so; \
                 ls -l /usr/lib/libQt6Gui*'
                 

# copy the actual file present in your sysroot
scp "$SYSROOT/usr/lib/libpcre2-16.so.0.12.0" $BOARD:/usr/lib/ || { echo "scp failed"; exit 1; }

# create soname symlinks on the board and list the result
ssh $BOARD 'ln -sf /usr/lib/libpcre2-16.so.0.12.0 /usr/lib/libpcre2-16.so.0; \
            ln -sf /usr/lib/libpcre2-16.so.0.12.0 /usr/lib/libpcre2-16.so; \
            ls -l /usr/lib/libpcre2-16*'
            

# copy the exact DBus library
scp "$SYSROOT/usr/lib/libQt6DBus.so.6.6.3" $BOARD:/usr/lib/ || { echo "scp failed"; exit 1; }

# create soname symlinks on the board and list result
ssh $BOARD 'ln -sf /usr/lib/libQt6DBus.so.6.6.3 /usr/lib/libQt6DBus.so.6; \
            ln -sf /usr/lib/libQt6DBus.so.6.6.3 /usr/lib/libQt6DBus.so; \
            ls -l /usr/lib/libQt6DBus*'
            

# copy the plugins directory (includes platforms, imageformats, etc.)
scp -r "$SYSROOT/usr/lib/plugins" $BOARD:/usr/lib/

# quick confirmation (host side)
echo "Copied plugins dir. Remote listing (will show after ssh):"
ssh $BOARD 'ls -l /usr/lib/plugins | sed -n "1,200p"'


echo "Looking for Qt Wayland client lib in sysroot..."
find "$SYSROOT/usr" -type f -name 'libQt6WaylandClient.so*' -print -exec ls -l {} \; | sed -n '1,120p'

# copy the actual file(s) found (adjust name if find shows a different version)
scp "$SYSROOT/usr/lib/libQt6WaylandClient.so.6.6.3" $BOARD:/usr/lib/ || \
  scp "$(find "$SYSROOT/usr" -type f -name 'libQt6WaylandClient.so*' -print | head -n1)" $BOARD:/usr/lib/

# create soname symlinks on the board and list result
ssh $BOARD 'ln -sf /usr/lib/libQt6WaylandClient.so.6.6.3 /usr/lib/libQt6WaylandClient.so.6 2>/dev/null || true; \
            ln -sf /usr/lib/libQt6WaylandClient.so.6.6.3 /usr/lib/libQt6WaylandClient.so 2>/dev/null || true; \
            ls -l /usr/lib/libQt6WaylandClient* || true'
