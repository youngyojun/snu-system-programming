import os, sys

files = os.listdir()

for filename in files:
	if ".wma" == filename[-4:]:
		filename = filename[:-4]

		os.system("ffmpeg -y -i {0}.wma {0}.wav".format(filename))
		os.system("ffmpeg -y -i {0}.wav -af 'volume=30' {0}-large.wav".format(filename))
		os.system("mv {0}-large.wav {0}.wav".format(filename))
