import os, sys

files = os.listdir()

# for filename in files:
# 	if ".wma" == filename[-4:]:
# 		filename = filename[:-4]
# 
# 		os.system("ffmpeg -y -i {0}.wma {0}.wav".format(filename))
# 		os.system("ffmpeg -y -i {0}.wav -af 'volume=30' {0}-large.wav".format(filename))
# 		os.system("mv {0}-large.wav {0}.wav".format(filename))

for filename in files:
	if ".wav" == filename[-4:]:
		filename = filename[:-4]

		os.system("ffmpeg -i {0}.wav -b:a 128k {0}.mp3".format(filename))

