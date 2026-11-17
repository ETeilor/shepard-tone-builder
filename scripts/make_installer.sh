#!/bin/bash
# Builds the macOS installer from the Release artefacts:
#   Shepard Tone Builder.component -> /Library/Audio/Plug-Ins/Components
#   Shepard Tone Builder.vst3      -> /Library/Audio/Plug-Ins/VST3
#   Shepard Tone Builder.app       -> /Applications
# Result: dist/ShepardToneBuilder-<ver>.pkg and dist/ShepardToneBuilder-<ver>.dmg (the dmg holds the pkg and a readme).
set -euo pipefail
VER="${1:-0.2.1}"
ART="build/ShepardToneBuilder_artefacts/Release"
OUT="dist"
WORK="$(mktemp -d)"
HERE="$(cd "$(dirname "$0")" && pwd)"
rm -rf "$OUT" && mkdir -p "$OUT"

stage() {   # stage <bundle> <subdir>
    mkdir -p "$WORK/$2"
    cp -R "$1" "$WORK/$2/"
    # Apple Silicon refuses unsigned code: ad-hoc signature (no Apple certificate needed)
    codesign --force --deep --sign - "$WORK/$2/$(basename "$1")"
    codesign --verify --deep --strict "$WORK/$2/$(basename "$1")"
}
stage "$ART/AU/Shepard Tone Builder.component" au
stage "$ART/VST3/Shepard Tone Builder.vst3" vst3
stage "$ART/Standalone/Shepard Tone Builder.app" app

mkdir -p "$WORK/scripts"
cp "$HERE/postinstall" "$WORK/scripts/postinstall"
chmod +x "$WORK/scripts/postinstall"

for part in au:Components vst3:VST3 app:; do
    name="${part%%:*}"
    dir="${part#*:}"
    loc="/Library/Audio/Plug-Ins/$dir"
    [ "$name" = app ] && loc="/Applications"
    pkgbuild --root "$WORK/$name" --install-location "$loc" --scripts "$WORK/scripts" \
             --identifier "com.teilormusic.shepardtonebuilder.$name" --version "$VER" "$WORK/$name.pkg"
done

cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Shepard Tone Builder $VER</title>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="au"/><line choice="vst3"/><line choice="app"/>
    </choices-outline>
    <choice id="au" title="Audio Unit (Logic Pro)"><pkg-ref id="com.teilormusic.shepardtonebuilder.au"/></choice>
    <choice id="vst3" title="VST3"><pkg-ref id="com.teilormusic.shepardtonebuilder.vst3"/></choice>
    <choice id="app" title="Standalone app"><pkg-ref id="com.teilormusic.shepardtonebuilder.app"/></choice>
    <pkg-ref id="com.teilormusic.shepardtonebuilder.au">au.pkg</pkg-ref>
    <pkg-ref id="com.teilormusic.shepardtonebuilder.vst3">vst3.pkg</pkg-ref>
    <pkg-ref id="com.teilormusic.shepardtonebuilder.app">app.pkg</pkg-ref>
</installer-gui-script>
XML
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" "$OUT/ShepardToneBuilder-$VER.pkg"

mkdir -p "$WORK/dmg"
cp "$OUT/ShepardToneBuilder-$VER.pkg" "$WORK/dmg/Install Shepard Tone Builder.pkg"
cat > "$WORK/dmg/Read me.txt" <<'TXT'
Shepard Tone Builder: установка

1. Дважды щёлкните «Install Shepard Tone Builder.pkg».
   Если macOS пишет, что разработчик не установлен: System Settings → Privacy & Security →
   внизу «Open Anyway», затем снова откройте установщик.
2. Установщик положит плагины в /Library/Audio/Plug-Ins (AU и VST3) и приложение в Applications.
3. Перезапустите Logic Pro.
   Плагин: дорожка программного инструмента → AU Instruments → Teilormusic → Shepard Tone Builder.
   Выберите режим (Тональный или Темповой), настройте параметры, нажмите «Слушать»
   и потяните карточку «MIDI в трек» на дорожку Logic. Регион нужной длины создастся сам.
   Звук даёт любой инструмент на этой дорожке. Темп проекта в ноты не входит.
   Если Logic плагин не показал: Logic Pro → Settings → Plug-in Manager, выберите плагин,
   нажмите «Reset & Rescan Selection».
TXT
hdiutil create -volname "Shepard Tone Builder $VER" -srcfolder "$WORK/dmg" -ov -format UDZO "$OUT/ShepardToneBuilder-$VER.dmg"
ls -la "$OUT"
