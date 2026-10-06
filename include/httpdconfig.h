
//Define this if you want to be able to use Heatshrink-compressed espfs images.
#define EFS_HEATSHRINK

//Pos of esp fs in flash
#ifndef ESPFS_POS
#define ESPFS_POS 0x7C000
#endif

//If you want, you can define a realm for the authentication system.
//#define HTTP_AUTH_REALM "MyRealm"