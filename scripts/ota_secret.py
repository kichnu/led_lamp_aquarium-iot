# Hasło OTA poza konfiguracją projektu.
#
# ${sysenv.OTA_PASSWORD_...} w platformio.ini wchodzi do project.checksum — inna wartość
# zmiennej (np. shell bez ~/.secrets/iot.env) = PlatformIO kasuje cały .pio/build i buduje
# oba środowiska od zera. Tu hasło trafia do wygenerowanego nagłówka (zmiana przebudowuje
# tylko main.cpp) i do flag espota; flagi kompilatora i checksum zostają stałe.
#
# Nazwa zmiennej: custom_ota_password_var w platformio.ini (po IP urządzenia, wspólny
# ~/.secrets/iot.env). Upload bez zmiennej = błąd (inaczej lampa dostaje puste hasło
# i każde kolejne OTA kończy się "Authentication Failed"); sam build = ostrzeżenie.

import os

Import("env")

var = env.GetProjectOption("custom_ota_password_var")

# OTA: zmienna wg IP docelowego — `--upload-port 192.168.10.6` → OTA_PASSWORD_192_168_10_6,
# więc wybór lampy = wybór hasła (wgrywanego i do --auth). USB: custom_ota_password_var.
if env.subst("$UPLOAD_PROTOCOL") == "espota":
    port = env.subst("$UPLOAD_PORT")
    if port:
        var = "OTA_PASSWORD_" + port.replace(".", "_")

password = os.environ.get(var, "")
uploading = any(t in COMMAND_LINE_TARGETS for t in ("upload", "uploadfs"))

if not password:
    if uploading:
        print("\n*** %s nie ustawione — upload przerwany (source ~/.secrets/iot.env)\n" % var)
        env.Exit(1)
    print("*** UWAGA: %s nie ustawione — firmware z pustym hasłem OTA (tylko build)" % var)

gen_dir = os.path.join(env.subst("$BUILD_DIR"), "generated")
header = os.path.join(gen_dir, "ota_secret.h")
escaped = password.replace("\\", "\\\\").replace('"', '\\"')
content = '#pragma once\n// Wygenerowane przez scripts/ota_secret.py — nie edytować\n#define OTA_PASSWORD "%s"\n' % escaped

# Zapis tylko przy zmianie — stały mtime = brak rekompilacji main.cpp
old = None
if os.path.isfile(header):
    with open(header, encoding="utf8") as f:
        old = f.read()
if old != content:
    os.makedirs(gen_dir, exist_ok=True)
    with open(header, "w", encoding="utf8") as f:
        f.write(content)

env.Append(CPPPATH=[gen_dir])

if env.subst("$UPLOAD_PROTOCOL") == "espota" and password:
    env.Append(UPLOAD_FLAGS=["--auth=" + password])
