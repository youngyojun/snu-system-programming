import os, sys


for i in range(1, 17):
	name = '0' + str(i) if i < 10 else str(i)

	print("RUN TEST {}".format(name))

	os.system("make test{} > outputs/test{}.txt".format(name, name))
	os.system("make rtest{} > outputs/rtest{}.txt".format(name, name))
