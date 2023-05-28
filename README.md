# framecontrol-obsmmf
An OBS plugin that receives a memory-map-file bitmap image from FrameControl

## bugs
May randomly cause OBS to crash. 

The crash source is either after closing OBS (releasing the textures throws due to heap corruption... could be due to the MMF header being random garbage causing but i dunno)#

The other crash is by having 2 FrameControl instances open with active MMF outputs, have OBS read from file 1, then change to file 2 by selecting the Map Name and pasting; it will pretty much crash every time

## Licence
As per obs studio's licence, all files in this repo are licenced under GPLv2 unless specified otherwise
