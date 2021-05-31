/*
  Please see;
  https://www.kernel.org/doc/Documentation/networking/tuntap.txt
*/

/* Flags: IFF_TUN   - TUN device (no Ethernet headers) 
 *        IFF_TAP   - TAP device  
 *
 *        IFF_NO_PI - Do not provide packet information  
 */ 
int tun_alloc(char const* dev, int flags);
int get_mtu(char const* dev);
