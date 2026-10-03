"""Compile the actual frontend SMB flow with filesystem and RPC fixtures.
Does not emulate the IOP TCP/SMB protocol or replace a PS2/server test.
"""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'src/platform/ps2/system/mainloop_smb.cpp').read_text()
prefix=r'''
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <vector>
#include "ps2smb.h"
#include "mainloop_smb.h"
char _MainLoop_BootDir[256]="";
char *_MainLoop_NetConfigPaths[]={nullptr};
static int usbEnabled,usbLoaded,sdEnabled,sdLoaded,sdLoads,smbEnabled=1;
static int driverResult,netReady=1,netInit=1,netConfig=1,logonResult,shareResult;
static int scopes,hashes,logons,shares,logoffs;
int HddMapPath(const char*,char*,int) {return -1;}
int MassStorageIsEnabled() {return usbEnabled;}
int Mx4sioIsEnabled() {return sdEnabled;}
int UsbBdmIsLoaded() {return usbLoaded;}
int Mx4sioIsLoaded() {return sdLoaded;}
int UsbBdmLoadEmbeddedIrx() {usbLoaded=usbEnabled;return usbEnabled?0:-1;}
int Mx4sioLoadIfEnabled() {++sdLoads;sdLoaded=sdEnabled;return sdEnabled?0:-1;}
int MmceSupportIsEnabled() {return 0;}
int MmceNeedsRestart() {return 0;}
int MmceGetAvailableSlots() {return 0;}
int MmceProbeAvailableSlots() {return 0;}
int CdfsIsLoaded() {return 0;}
int CdfsLoadEmbeddedIrx() {return -1;}
int SmbSupportIsEnabled() {return smbEnabled;}
void SmbSupportSetEnabled(int v) {smbEnabled=v;}
int SmbLoadEmbeddedIrx() {return driverResult;}
int _MainLoopInitNetwork(char**) {return netInit;}
int _MainLoopConfigureNetwork(char**,char*) {return netConfig;}
int _MainLoopWaitForNetwork(int timeout) {assert(timeout==15000);return netReady;}
void BgmIOBegin() {++scopes;}
void BgmIOEnd() {assert(scopes);--scopes;}
int fileXioDevctl(const char *dev,int cmd,void *in,unsigned len,void *out,unsigned outlen) {
 assert(!strcmp(dev,"smb:"));
 if(cmd==SMB_DEVCTL_GETPASSWORDHASHES) {
  assert(len==sizeof(smbGetPasswordHashes_in_t) && outlen==32);
  assert(!strcmp(((smbGetPasswordHashes_in_t*)in)->password,"fixture-secret"));
  for(unsigned i=0;i<32;++i) ((u8*)out)[i]=i+1;
  ++hashes;return 0;
 }
 if(cmd==SMB_DEVCTL_LOGON) {
  auto p=(smbLogOn_in_t*)in;assert(len==sizeof(*p));
  assert(!strcmp(p->serverIP,"192.168.0.2") && p->serverPort==445);
  assert(!strcmp(p->User,"fixture-user"));
  if(p->PasswordType==HASHED_PASSWORD) for(unsigned i=0;i<32;++i) assert((u8)p->Password[i]==i+1);
  else if(p->PasswordType==PLAINTEXT_PASSWORD) assert(!strcmp(p->Password,"fixture-secret"));
  ++logons;return logonResult;
 }
 if(cmd==SMB_DEVCTL_OPENSHARE) {
  auto p=(smbOpenShare_in_t*)in;assert(len==sizeof(*p) && !strcmp(p->ShareName,"ROMS"));
  ++shares;return shareResult;
 }
 if(cmd==SMB_DEVCTL_LOGOFF) {++logoffs;return 0;}
 assert(cmd==SMB_DEVCTL_CLOSESHARE);return 0;
}
'''
suffix=r'''
static void configFile(const char *path,const char *extra="") {
 FILE *f=fopen(path,"wb");assert(f);
 fprintf(f,"SERVER_IP=192.168.0.2\nSERVER_PORT=445\nSHARE=ROMS\nUSER=fixture-user\nPASSWORD=fixture-secret\n%s",extra);
 assert(!fclose(f));
}
int main() {
 assert(sizeof(smbLogOn_in_t)==536 && sizeof(smbOpenShare_in_t)==516);
 int number;assert(SmbParseInteger("445",&number)==0 && number==445);
 assert(SmbParseInteger("4294967741",&number)<0); // wraps to 445 on a 32-bit int
 assert(SmbParseInteger("99999999999999999999999999",&number)<0);
 assert(SmbParseInteger("445junk",&number)<0);
 SmbConfigT config;
 assert(SmbLoadCurrentConfig(&config)==0);
 assert(SmbEnsureMounted()<0 && !strcmp(SmbGetStatusText(),"No SMB.CNF"));
 assert(!mkdir("mass9:",0777));assert(!mkdir("mass9:/SNESticle",0777));
 configFile("mass9:/SNESticle/SMB.CNF");
 sdEnabled=1;
 assert(SmbLoadCurrentConfig(&config)==1 && sdLoads==1 && sdLoaded);
 assert(!strcmp(SmbGetConfigPath(),"mass9:/SNESticle/SMB.CNF"));
 assert(SmbEnsureMounted()==0 && SmbIsMounted() && hashes==1 && logons==1 && shares==1);
 assert(SmbEnsureMounted()==0 && logons==1); // already mounted: no new RPC
 SmbDisconnect();assert(!SmbIsMounted());
 for(int error:{SMB_DEVCTL_LOGON_ERR_CONN,SMB_DEVCTL_LOGON_ERR_PROT,SMB_DEVCTL_LOGON_ERR_LOGON}) {
  logonResult=-error;assert(SmbEnsureMounted()<0 && !SmbIsMounted());
  const char *text=error==SMB_DEVCTL_LOGON_ERR_CONN?"Connect Error":error==SMB_DEVCTL_LOGON_ERR_PROT?"SMB1 Required":"Auth Error";
  assert(!strcmp(SmbGetStatusText(),text));
 }
 logonResult=0;shareResult=-9;
 int old=logoffs;assert(SmbEnsureMounted()<0 && logoffs==old+1 && !strcmp(SmbGetStatusText(),"Share Error"));
 shareResult=0;netReady=0;assert(SmbEnsureMounted()<0 && !strcmp(SmbGetStatusText(),"DHCP Timeout"));netReady=1;
 driverResult=-5;assert(SmbEnsureMounted()<0 && !strcmp(SmbGetStatusText(),"Driver Error"));driverResult=0;
 configFile("mass9:/SNESticle/SMB.CNF","PASSWORD_TYPE=0\n");
 old=hashes;assert(SmbEnsureMounted()==0 && hashes==old);SmbDisconnect();
 assert(SmbSaveAndConnect(&config)==0 && !scopes);SmbDisconnect();
 config.serverPort=999999;assert(SmbSaveAndConnect(&config)<0 && !scopes);
 // An owned multitap-card file can override mass storage; old IDs/paths stay valid.
 assert(!mkdir("mc7:",0777));assert(!mkdir("mc7:/SNESticle",0777));
 configFile("mc7:/SNESticle/SMB.CNF");assert(SmbLoadCurrentConfig(&config)==1);
 assert(!strcmp(SmbGetConfigPath(),"mc7:/SNESticle/SMB.CNF"));
 assert(SmbReadConfigFile("missing",&config)==0);
 FILE *f=fopen("bad.cnf","wb");assert(f);fputs("SERVER_PORT=4294967741\n",f);fclose(f);
 assert(SmbReadConfigFile("bad.cnf",&config)<0);
 puts("SMB frontend: PASS (config, integer bounds, MX4SIO, mass9/mc7, RPC ABI/order, failure recovery)");
}
'''
with tempfile.TemporaryDirectory(prefix='smb-flow-') as tmp:
 p=Path(tmp);cpp=p/'fixture.cpp';cpp.write_text(prefix+source[source.index('enum SmbStatusE'):]+suffix)
 subprocess.run(['g++','-std=c++17','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'tools/smbtest/include'),'-I'+str(root/'src/platform/ps2/system'),str(cpp),'-o',str(p/'fixture')],check=True)
 subprocess.run([str(p/'fixture')],cwd=p,check=True)
