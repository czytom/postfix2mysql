# first, the imports
import os

# then the install paths
def unfuck(path):
	while "//" in path: path = path.replace("//","/")
	return path
prefix  = os.getenv("PREFIX","/usr/local")
destdir = os.getenv("DESTDIR","/")
bindir  = unfuck(os.path.join( destdir+prefix, "sbin" ))
mandir  = unfuck(os.path.join( destdir+prefix, "man8" ))

# then the environment
env = Environment()
g = Builder(action = "gzip -9 < $SOURCE > $TARGET")
env.Append(BUILDERS={"Gzip":g})
env.Decider('timestamp-newer')

# then the targets
unique = [ env.Object( t ) for t in env.Glob("mysqmail-*-logger.c") ]
common = [ env.Object( t ) for t in env.Split("mydaemon.c myconfig.c") ]

# then the dynamic libs
libs = ['mysqlclient', 'dotconf']

# then the programs
programs = [ env.Program( [ t ] + common, LIBS = libs) for t in unique ]

# then the manpages
manpages = [ env.Gzip("%s.gz"%s,s) for s in env.Glob("doc/*.8") ]

# then the installation
all = [ env.Install(k,v) for k,v in {bindir:programs,mandir:manpages}.items() ]

env.Alias('install', all)
