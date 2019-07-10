import os

dirs = ['inc','kernel','user']

# clang-format is not able to handle assembly or makefile
ignorelist = ['Makefrag', 'trap_support.h', 'entry.S',
              'vectors.S', 'trapasm.S', 'bootasm.S',
              'swtch.S', 'initcode.S', 'kernel.lds.S']

for d in dirs:
    files = os.listdir(d)
    for filename in files:
        if filename in ignorelist:
            continue
        if filename.endswith(".formatted"):
            continue
        filepath = os.path.join(d, filename)
        formatedpath = filepath + ".formatted"
        os.system("clang-format %s > %s" % (filepath, formatedpath))


for d in dirs:
    files = os.listdir(d)
    for filename in files:
        if not filename.endswith(".formatted"):
            continue
        formatedpath = os.path.join(d, filename)
        filepath = formatedpath[:len(formatedpath) - len(".formatted"  )]
        os.system("mv %s %s" % (formatedpath, filepath))
