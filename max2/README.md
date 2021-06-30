nisetro_wsc HDL code

 - Video
 
|NAME|WSC CN2|MAX2 PIN#
|--|--|--|
|DCLK0|22|-
|DCLK1|21|CN2_CLK62
|HSYNC|23|CN1_IO77
|VSYNC|18|CN1_IO76
|GND|19|CN1_GND
|RGB0_D0|14|CN1_IO81
|RGB0_D1|15|CN1_IO78
|RGB0_D2|16|CN1_IO95
|RGB0_D3|17|CN1_IO92
|RGB1_D0|10|CN1_IO89
|RGB1_D1|11|CN1_IO91
|RGB1_D2|12|CN1_IO83
|RGB1_D3|13|CN1_IO82
|RGB2_D0|6|CN1_IO84
|RGB2_D1|7|CN1_IO85
|RGB2_D2|8|CN1_IO86
|RGB2_D3|9|CN1_IO87

 - Audio

|NAME|MAX2 PIN#
|--|--|
|**BCLK**|**CN1_CLK64**|
|**RESET**|**CN2_IO67**|
|LRCK|CN2_IO66|
|SDAT|CN2_IO69|
|SEN  |CN2_IO68|

If you are going to wire up between WSC and カメレオンUSB FX2 without using my adapter board, remember to connect BCLK and RESET signal even if you just only want to capture video signals. And also need to pull RESET signal to GND with a 10K ohm resistor.
For the location of BCLK and RESET signal, please check photo in eagle directory.

HDL code is based on [偽トロキャプチャと車とかとか (Web Archive Link)](https://web.archive.org/web/20151204081747/http://pipin.blog.eonet.jp/default/2010/05/i-531-e77e.html)