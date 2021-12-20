mkdir -p /Library/CoreMediaIO/Plug-Ins/DAL
rm -rf /Library/CoreMediaIO/Plug-Ins/DAL/vizard-mac-virtualcam.plugin
cp -R "$1/vizard-mac-virtualcam.plugin" /Library/CoreMediaIO/Plug-Ins/DAL
