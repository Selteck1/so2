#!/system/bin/sh
# inject.sh — используем manualmap_injector с нашим libximira.so
# запуск: su -c "sh /storage/emulated/0/xim_test/inject.sh"

SRC="/storage/emulated/0/xim_test"
DST="/data/local/tmp/ximira"

echo "[*] staging: $DST"
mkdir -p "$DST"
chmod 777 "$DST"

if [ ! -f "$SRC/manualmap_injector" ]; then
    echo "[!] FATAL: manualmap_injector not in $SRC"
    exit 1
fi
if [ ! -f "$SRC/libximira.so" ]; then
    echo "[!] FATAL: libximira.so not in $SRC"
    exit 1
fi

cp -f "$SRC/manualmap_injector" "$DST/manualmap_injector"
cp -f "$SRC/libximira.so"       "$DST/libximira.so"

chmod 755 "$DST/manualmap_injector"
chmod 755 "$DST/libximira.so"

chcon u:object_r:apk_data_file:s0 "$DST/manualmap_injector" 2>/dev/null || true
chcon u:object_r:apk_data_file:s0 "$DST/libximira.so"       2>/dev/null || true

PID=$(pidof com.axlebolt.standoff2 2>/dev/null || echo "")
if [ -z "$PID" ]; then
    echo "[!] Standoff 2 не запущен"
    exit 1
fi
echo "[*] Standoff 2 PID = $PID"

echo "[*] SELinux = before: $(getenforce)"
setenforce 0 2>/dev/null || true
echo "[*] SELinux = after:  $(getenforce)"

cd "$DST"

echo ""
echo "==============================================="
echo "[*] injecting libximira.so"
echo "==============================================="
./manualmap_injector "$PID" "$DST/libximira.so"
RC=$?
echo "[*] exit code: $RC"

echo ""
echo "[*] check logcat:"
echo "[*]   su -c \"logcat -s Ximira:V\""