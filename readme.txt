Install the toolchain following:

https://github.com/espressif/arduino-esp32

(which really is just download the repo to specific folder and run get.py)

If the drivers don't autmatically load when connecting with USB, follow:

https://wiki.wemos.cc/downloads

BILL OF MATERIALS:

Descriptoin	Part Number	Cost	Shipping	Quantity	Total
Microprocessor	Lolin32 lite	$4.90	$1.81		1		$4.90
Accelerometer	LIS3DH		$4.95	$10		1		$4.95
Battery		2000mAh		$12.50	^		1		$12.50
Motherboard			$4.95	free		1		$4.95
Case				$19.76	free		1		$19.76
Magnets				$0.26	free		2		$0.52
Headers	Female			$0.04	^		36		$1.59
Headers	Male			$0.01	^		36		$0.50

Needed Libraries Included

Gerber files for motherboard in zip file included

STL for enclosure included (needes updated)

To Use End Product:
Power device on (connect battery)
Use smartphone or computer to connect to SSID 'WashingMachine'
If captive portal doesn't launch, point browser to 192.168.4.1
Follow prompts, select your SSID, enter Wifi password, Ifttt Event Name, and Ifttt Maker Key
    Ifttt maker key can be found at https://ifttt.com/maker_webhooks, click Documentation
    Make sure to add the webhooks service first, and create the applet you want fired
To restore to factory settings, press Reset button or power off device


Values to change in WashingMachineNotify.ino:
#define TIME_TO_SLEEP  300      /* Snooze time between re-notifies */
#define AWAKE_TIME 10000 //milliseconds to wait for another vibration before sleep
#define NOTIFY_THRESHOLD 270000 //milliseconds of vibration before sending notification
#define TIMEOUT 120 //seconds for capture portal to be active
#define NOTIFY_DELAY 120 //seconds to wait before sending first notify

Todo: 
Update to Wemos Lolin32 (not lite)


Sources:
Accelerometer - https://github.com/alexspurling/washingmachine
WifiManger - https://github.com/zhouhan0126/WIFIMANAGER-ESP32
