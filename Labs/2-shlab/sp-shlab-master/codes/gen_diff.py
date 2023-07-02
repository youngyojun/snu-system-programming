import os, sys

for i in range(1, 17):
	name = '0' + str(i) if i < 10 else str(i)

	print("DIFF TEST {}".format(name))

	os.system("diff outputs/rtest{}.txt outputs/test{}.txt > diff_results/diff{}.txt".format(name, name, name))
