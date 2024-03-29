

This directory contains EAGLE schematic and board files for the nisetro_wsc.

The zip file is a gerber zipped file so you can build PCB with that.

I also uploaded the gerber zip file to [OSHPARK](https://oshpark.com/shared_projects/rcBuRHa7) so you can order PCB from there.

This version of adapter PCB removed two main buffer ICs so it should be much easier to build than previous version.

Also fixed the following problems:

 1. Screen will glitch after inserting first version (with cable) of headphone adapter.
 2. Device will be hard to turn on or off after inserting some type of cartridges.

 - BOM

|Part|Package|Value|Mouser|Qty|Note
|--|--|--|--|--|--|
|C7|C1206|10uF 16V|581-TAJA106K016RNJV|1|Tantalum Capacitors
|C8|C1206|100uF 4V|581-TAJA107K004RNJ|1|Tantalum Capacitors
|R1, R4|R0805|10K|603-RT0805FRE0710KL|2|
|R5|R0805|330|603-RT0805FRE07330RL|1|
|Q2|SOT-23-3|MMBT2222A|621-MMBT2222A-F|1| 
|U3|SOT-223-4|TLV1117-15CDCYR|595-TLV1117-15CDCYR|1|LDO for powering WSC (1.5V)
|CN3|||710-68712414022|1|FPC connector (Top contact)
|CN3 (alternative)|||710-68712414522|1|FPC connector (Bottom contact)
|R2, R3|R0805|10K|603-RT0805FRE0710KL|2| Optional if you want to install LED
|Q1|SOT-23-3|MMBT2222A|621-MMBT2222A-F|1| Optional if you want to install LED
|PWR|0603|Green LED|743-IN-S63BTG|1| Optional if you want to install LED. You can also choose the color you like, but make sure package is 0603.
||||710-687624100002|1|FPC cable (Same Side Contacts)
||||710-687724100002|1|FPC cable (Opposite Side Contacts)

U2 and C1 are for development purpose, please ignore them.

![PCB top](https://raw.githubusercontent.com/splash5/nisetro_wsc/main/eagle/nisetoro_wsc_adapter_v3_top.png)
![PCB bottom](https://raw.githubusercontent.com/splash5/nisetro_wsc/main/eagle/nisetoro_wsc_adapter_v3_bottom.png)

You will also need a 0.5mm pitch, 24pins FPC cable to connect from WSC CN2 (LCD connector) to adapter PCB CN3. The length should be at least 80mm long, or it will be very hard to install. I have tested with 150mm one and works ok, but if cable is too long the signal may not capture properly. The FPC cable also have two types: same side or opposite contacts. Choose one which makes installation easier, but **be sure that pin #1 of WSC CN2 connect to adapter board CN3 pin #1**, or you might damage your WSC or adapter board.

After soldering all the parts on the adapter PCB, it should looks like this: (Please ignore U2 and C1, that is for development purpose.)
![PCB finished](https://raw.githubusercontent.com/splash5/nisetro_wsc/main/eagle/pcb_completed.jpg)

Now you will need to connect at least RESET and BCLK to WSC to make everything work. LRCK, SDAT and SEN is for audio capturing. ON1 and ON2 are optional if you want to install a tactile switch as power switch. "TO WSC" is 1.5V output, connect V1 and VS to WSC power supply module V1 and VS pin.

![solder points top](https://raw.githubusercontent.com/splash5/nisetro_wsc/main/eagle/wsc_top.jpg)
![solder points bottom](https://raw.githubusercontent.com/splash5/nisetro_wsc/main/eagle/wsc_bottom.jpg)
