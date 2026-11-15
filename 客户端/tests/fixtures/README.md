# Music test fixture

`silence.mp3` contains approximately one second of silent stereo audio, generated locally from zero PCM samples using the Windows Media Foundation MP3 encoder. It contains no third-party recording.

The native music tests always set playback volume to zero and use a separate `music-test-<process id>` directory beside the test executable. They do not read or modify the user's real music library.
