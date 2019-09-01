# BroseFlipDotESP8266

Few months ago I bought some very old and dirty Brose Vollmatrix displays also known as flip dot of disk displays. With the use of the internet I reverse engineered the internals and hooked up an ESP8266 board. Using an MQTT broker I'm able to display text and bitmaps (black and white e.g. 112x16 pixels). After some time the display will enter idle mode where is makes a nice clock. The display also contains a backlight that can be controlled using MQTT.



MQTT enabled Brose FlipDot

{
	"message": {
		"content": "DIEDERICH",
		"x": 0,
		"y": 0,
		"font": 2,
		"display_time": 20
	},
	"backlight": {
		"on": true
	}
}
