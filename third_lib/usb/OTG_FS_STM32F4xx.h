/* -----------------------------------------------------------------------------
 * OTG_FS_STM32F4xx.h - USB OTG Full-Speed register definitions for STM32F405/407
 *
 * Reconstructed for the Keil CMSIS-Driver "USBD_FS_STM32F4xx.c" (V2.21).
 * Register layout and bit positions follow the official STM32CubeF4
 * CMSIS device header (stm32f405xx.h) for the STM32F407.
 * -------------------------------------------------------------------------- */
#ifndef OTG_FS_STM32F4xx_H_
#define OTG_FS_STM32F4xx_H_

#include "stm32f4xx.h"

#define OTG_FS_BASE                (0x50000000U)

/* The Keil USB driver uses these CMSIS helpers; the project's old
 * core_cm4.h does not provide them. */
#ifndef __UNALIGNED_UINT32_READ
#if defined ( __CC_ARM )
typedef __packed union { uint32_t v; } __OTG_UNALIGNED_U32;
#define __UNALIGNED_UINT32_READ(addr)   ((const __OTG_UNALIGNED_U32 *)(addr))->v
#define __UNALIGNED_UINT32_WRITE(addr, val) ((__OTG_UNALIGNED_U32 *)(addr))->v = (uint32_t)(val)
#else
#define __UNALIGNED_UINT32_READ(addr)   (*(const uint32_t *)(addr))
#define __UNALIGNED_UINT32_WRITE(addr, val) (*(uint32_t *)(addr) = (uint32_t)(val))
#endif
#endif

/* Pin configuration hooks used by USBD_FS_STM32F4xx.c (implemented in usb_mtp.c) */
void OTG_FS_PinsConfigure(uint32_t pins);
void OTG_FS_PinsUnconfigure(uint32_t pins);

/* The Keil driver waits with HAL_Delay (no HAL in this project) */
#ifndef HAL_Delay
extern void HAL_Delay(uint32_t Delay);
#endif

/* RCC bits used by the driver (already defined in SPL headers, guarded) */
#ifndef RCC_AHB2ENR_OTGFSEN
#define RCC_AHB2ENR_OTGFSEN        ((uint32_t)0x00000080)
#endif
#ifndef RCC_AHB2RSTR_OTGFSRST
#define RCC_AHB2RSTR_OTGFSRST      ((uint32_t)0x00000080)
#endif

/** \brief USB OTG FS core register map (device mode subset) */
typedef struct
{
  __IO uint32_t GOTGCTL;            /*!< 0x000 OTG control and status       */
  __IO uint32_t GOTGINT;            /*!< 0x004 OTG interrupt                */
  __IO uint32_t GAHBCFG;            /*!< 0x008 AHB configuration            */
  __IO uint32_t GUSBCFG;            /*!< 0x00C USB configuration            */
  __IO uint32_t GRSTCTL;            /*!< 0x010 Core reset                   */
  __IO uint32_t GINTSTS;            /*!< 0x014 Core interrupt status        */
  __IO uint32_t GINTMSK;            /*!< 0x018 Core interrupt mask          */
  __IO uint32_t GRXSTSR;            /*!< 0x01C Receive status (read only)   */
  __IO uint32_t GRXSTSP;            /*!< 0x020 Receive status pop           */
  __IO uint32_t GRXFSIZ;            /*!< 0x024 Receive FIFO size            */
  __IO uint32_t GNPTXFSIZ;          /*!< 0x028 Non-periodic Tx FIFO size    */
  __IO uint32_t HNPTXSTS;           /*!< 0x02C Non-periodic Tx FIFO status  */
  __IO uint32_t RESERVED30[2];      /*!< 0x030 - 0x037                      */
  __IO uint32_t GCCFG;              /*!< 0x038 General core configuration   */
  __IO uint32_t CID;                /*!< 0x03C Core ID                      */
  __IO uint32_t RESERVED40[48];     /*!< 0x040 - 0x0FF                      */
  __IO uint32_t HPTXFSIZ;           /*!< 0x100 Host periodic Tx FIFO size   */
  __IO uint32_t DIEPTXF0;           /*!< 0x104 Device EP0 Tx FIFO size      */
  __IO uint32_t DIEPTXF1;           /*!< 0x108 Device EP1 Tx FIFO size      */
  __IO uint32_t DIEPTXF2;           /*!< 0x10C Device EP2 Tx FIFO size      */
  __IO uint32_t DIEPTXF3;           /*!< 0x110 Device EP3 Tx FIFO size      */
  __IO uint32_t RESERVED114[443];   /*!< 0x114 - 0x7FF                      */
  __IO uint32_t DCFG;               /*!< 0x800 Device configuration         */
  __IO uint32_t DCTL;               /*!< 0x804 Device control               */
  __IO uint32_t DSTS;               /*!< 0x808 Device status                */
  __IO uint32_t RESERVED80C;        /*!< 0x80C                              */
  __IO uint32_t DIEPMSK;            /*!< 0x810 Device IN EP interrupt mask  */
  __IO uint32_t DOEPMSK;            /*!< 0x814 Device OUT EP interrupt mask */
  __IO uint32_t DAINT;              /*!< 0x818 Device all EP interrupt      */
  __IO uint32_t DAINTMSK;           /*!< 0x81C Device all EP mask           */
  __IO uint32_t RESERVED820[2];     /*!< 0x820 - 0x827                      */
  __IO uint32_t DVBUSDIS;           /*!< 0x828 Device VBUS discharge time   */
  __IO uint32_t DVBUSPULSE;         /*!< 0x82C Device VBUS pulse time       */
  __IO uint32_t DTHRCTL;            /*!< 0x830 Device threshold control     */
  __IO uint32_t DIEPEMPMSK;         /*!< 0x834 Device IN EP FIFO empty mask */
  __IO uint32_t RESERVED838[18];    /*!< 0x838 - 0x87F                      */
  __IO uint32_t RESERVED880[32];    /*!< 0x880 - 0x8FF                      */
  __IO uint32_t DIEPCTL0;           /*!< 0x900 Device IN EP0 control        */
  __IO uint32_t RESERVED904;        /*!< 0x904                               */
  __IO uint32_t DIEPINT0;           /*!< 0x908 Device IN EP0 interrupt      */
  __IO uint32_t RESERVED90C;        /*!< 0x90C                              */
  __IO uint32_t DIEPTSIZ0;          /*!< 0x910 Device IN EP0 transfer size  */
  __IO uint32_t DIEPDMA0;           /*!< 0x914 Device IN EP0 DMA address    */
  __IO uint32_t DTXFSTS0;           /*!< 0x918 Device IN EP0 Tx FIFO status */
  __IO uint32_t RESERVED91C;        /*!< 0x91C                              */
  __IO uint32_t DIEPCTL1;           /*!< 0x920 Device IN EP1 control        */
  __IO uint32_t RESERVED924;        /*!< 0x924                              */
  __IO uint32_t DIEPINT1;           /*!< 0x928 Device IN EP1 interrupt      */
  __IO uint32_t RESERVED92C;        /*!< 0x92C                              */
  __IO uint32_t DIEPTSIZ1;          /*!< 0x930 Device IN EP1 transfer size  */
  __IO uint32_t DIEPDMA1;           /*!< 0x934 Device IN EP1 DMA address    */
  __IO uint32_t DTXFSTS1;           /*!< 0x938 Device IN EP1 Tx FIFO status */
  __IO uint32_t RESERVED93C;        /*!< 0x93C                              */
  __IO uint32_t DIEPCTL2;           /*!< 0x940 Device IN EP2 control        */
  __IO uint32_t RESERVED944;        /*!< 0x944                              */
  __IO uint32_t DIEPINT2;           /*!< 0x948 Device IN EP2 interrupt      */
  __IO uint32_t RESERVED94C;        /*!< 0x94C                              */
  __IO uint32_t DIEPTSIZ2;          /*!< 0x950 Device IN EP2 transfer size  */
  __IO uint32_t DIEPDMA2;           /*!< 0x954 Device IN EP2 DMA address    */
  __IO uint32_t DTXFSTS2;           /*!< 0x958 Device IN EP2 Tx FIFO status */
  __IO uint32_t RESERVED95C;        /*!< 0x95C                              */
  __IO uint32_t DIEPCTL3;           /*!< 0x960 Device IN EP3 control        */
  __IO uint32_t RESERVED964;        /*!< 0x964                              */
  __IO uint32_t DIEPINT3;           /*!< 0x968 Device IN EP3 interrupt      */
  __IO uint32_t RESERVED96C;        /*!< 0x96C                              */
  __IO uint32_t DIEPTSIZ3;          /*!< 0x970 Device IN EP3 transfer size  */
  __IO uint32_t DIEPDMA3;           /*!< 0x974 Device IN EP3 DMA address    */
  __IO uint32_t DTXFSTS3;           /*!< 0x978 Device IN EP3 Tx FIFO status */
  __IO uint32_t RESERVED97C;        /*!< 0x97C                              */
  __IO uint32_t RESERVED980[96];    /*!< 0x980 - 0xAFF                      */
  __IO uint32_t DOEPCTL0;           /*!< 0xB00 Device OUT EP0 control       */
  __IO uint32_t RESERVEDB04;        /*!< 0xB04                              */
  __IO uint32_t DOEPINT0;           /*!< 0xB08 Device OUT EP0 interrupt     */
  __IO uint32_t RESERVEDB0C;        /*!< 0xB0C                              */
  __IO uint32_t DOEPTSIZ0;          /*!< 0xB10 Device OUT EP0 transfer size */
  __IO uint32_t DOEPDMA0;           /*!< 0xB14 Device OUT EP0 DMA address   */
  __IO uint32_t RESERVEDB18[2];     /*!< 0xB18 - 0xB1F                      */
  __IO uint32_t DOEPCTL1;           /*!< 0xB20 Device OUT EP1 control       */
  __IO uint32_t RESERVEDB24;        /*!< 0xB24                              */
  __IO uint32_t DOEPINT1;           /*!< 0xB28 Device OUT EP1 interrupt     */
  __IO uint32_t RESERVEDB2C;        /*!< 0xB2C                              */
  __IO uint32_t DOEPTSIZ1;          /*!< 0xB30 Device OUT EP1 transfer size */
  __IO uint32_t DOEPDMA1;           /*!< 0xB34 Device OUT EP1 DMA address   */
  __IO uint32_t RESERVEDB38[2];     /*!< 0xB38 - 0xB3F                      */
  __IO uint32_t DOEPCTL2;           /*!< 0xB40 Device OUT EP2 control       */
  __IO uint32_t RESERVEDB44;        /*!< 0xB44                              */
  __IO uint32_t DOEPINT2;           /*!< 0xB48 Device OUT EP2 interrupt     */
  __IO uint32_t RESERVEDB4C;        /*!< 0xB4C                              */
  __IO uint32_t DOEPTSIZ2;          /*!< 0xB50 Device OUT EP2 transfer size */
  __IO uint32_t DOEPDMA2;           /*!< 0xB54 Device OUT EP2 DMA address   */
  __IO uint32_t RESERVEDB58[2];     /*!< 0xB58 - 0xB5F                      */
  __IO uint32_t DOEPCTL3;           /*!< 0xB60 Device OUT EP3 control       */
  __IO uint32_t RESERVEDB64;        /*!< 0xB64                              */
  __IO uint32_t DOEPINT3;           /*!< 0xB68 Device OUT EP3 interrupt     */
  __IO uint32_t RESERVEDB6C;        /*!< 0xB6C                              */
  __IO uint32_t DOEPTSIZ3;          /*!< 0xB70 Device OUT EP3 transfer size */
  __IO uint32_t DOEPDMA3;           /*!< 0xB74 Device OUT EP3 DMA address   */
  __IO uint32_t RESERVEDB78[2];     /*!< 0xB78 - 0xB7F                      */
  __IO uint32_t RESERVEDB80[160];   /*!< 0xB80 - 0xDFF                      */
  __IO uint32_t PCGCCTL;            /*!< 0xE00 Power and clock gating       */
} USB_OTG_CORE_REGS;

#define OTG_FS   ((USB_OTG_CORE_REGS *)OTG_FS_BASE)

/* register bit definitions (from STM32CubeF4 stm32f405xx.h) */
#define OTG_FS_GOTGINT_SEDET   (0x1UL << 2U) /*!< 0x00000004 */
#define OTG_FS_GAHBCFG_TXFELVL   (0x1UL << 7U) /*!< 0x00000080 */
#define OTG_FS_GUSBCFG_PHYSEL   (0x1UL << 6U) /*!< 0x00000040 */
#define OTG_FS_GUSBCFG_FDMOD   (0x1UL << 30U) /*!< 0x40000000 */
#define OTG_FS_GUSBCFG_FHMOD   (0x1UL << 29U) /*!< 0x20000000 */
#define OTG_FS_GRSTCTL_AHBIDL   (0x1UL << 31U) /*!< 0x80000000 */
#define OTG_FS_GRSTCTL_CSRST   (0x1UL << 0U) /*!< 0x00000001 */
#define OTG_FS_GRSTCTL_TXFFLSH   (0x1UL << 5U) /*!< 0x00000020 */
#define OTG_FS_GINTSTS_SRQINT   (0x1UL << 30U) /*!< 0x40000000 */
#define OTG_FS_GINTSTS_OTGINT   (0x1UL << 2U) /*!< 0x00000004 */
#define OTG_FS_GINTSTS_SOF   (0x1UL << 3U) /*!< 0x00000008 */
#define OTG_FS_GINTSTS_RXFLVL   (0x1UL << 4U) /*!< 0x00000010 */
#define OTG_FS_GINTSTS_USBSUSP   (0x1UL << 11U) /*!< 0x00000800 */
#define OTG_FS_GINTSTS_USBRST   (0x1UL << 12U) /*!< 0x00001000 */
#define OTG_FS_GINTSTS_ENUMDNE   (0x1UL << 13U) /*!< 0x00002000 */
#define OTG_FS_GINTSTS_EOPF   (0x1UL << 15U) /*!< 0x00008000 */
#define OTG_FS_GINTSTS_IEPINT   (0x1UL << 18U) /*!< 0x00040000 */
#define OTG_FS_GINTSTS_OEPINT   (0x1UL << 19U) /*!< 0x00080000 */
#define OTG_FS_GINTSTS_IISOIXFR   (0x1UL << 20U) /*!< 0x00100000 */
#define OTG_FS_GINTSTS_IPXFR   (0x1UL << 21U) /*!< 0x00200000 */
#define OTG_FS_GINTSTS_GONAKEFF   (0x1UL << 7U) /*!< 0x00000080 */
#define OTG_FS_GINTSTS_WKUPINT   (0x1UL << 31U) /*!< 0x80000000 */
#define OTG_FS_GINTMSK_SRQIM   (0x1UL << 30U) /*!< 0x40000000 */
#define OTG_FS_GINTMSK_OTGINT   (0x1UL << 2U) /*!< 0x00000004 */
#define OTG_FS_GINTMSK_SOFM   (0x1UL << 3U) /*!< 0x00000008 */
#define OTG_FS_GINTMSK_RXFLVLM   (0x1UL << 4U) /*!< 0x00000010 */
#define OTG_FS_GINTMSK_USBSUSPM   (0x1UL << 11U) /*!< 0x00000800 */
#define OTG_FS_GINTMSK_USBRST   (0x1UL << 12U) /*!< 0x00001000 */
#define OTG_FS_GINTMSK_ENUMDNEM   (0x1UL << 13U) /*!< 0x00002000 */
#define OTG_FS_GINTMSK_EOPFM   (0x1UL << 15U) /*!< 0x00008000 */
#define OTG_FS_GINTMSK_IEPINT   (0x1UL << 18U) /*!< 0x00040000 */
#define OTG_FS_GINTMSK_OEPINT   (0x1UL << 19U) /*!< 0x00080000 */
#define OTG_FS_GINTMSK_IISOIXFRM   (0x1UL << 20U) /*!< 0x00100000 */
#define OTG_FS_GINTMSK_IPXFRM   (0x1UL << 21U) /*!< 0x00200000 */
#define OTG_FS_GINTMSK_WUIM   (0x1UL << 31U) /*!< 0x80000000 */
#define OTG_FS_GCCFG_PWRDWN   (0x1UL << 16U) /*!< 0x00010000 */
#define OTG_FS_GCCFG_VBUSBSEN   (0x1UL << 19U) /*!< 0x00080000 */
#define OTG_FS_GCCFG_NOVBUSSENS   (0x1UL << 21U) /*!< 0x00200000 */
#define OTG_FS_DCFG_DSPD   (0x3UL << 0U) /*!< 0x00000003 */
#define OTG_FS_DCTL_RWUSIG   (0x1UL << 0U) /*!< 0x00000001 */
#define OTG_FS_DCTL_SDIS   (0x1UL << 1U) /*!< 0x00000002 */
#define OTG_FS_DCTL_SGONAK   (0x1UL << 9U) /*!< 0x00000200 */
#define OTG_FS_DCTL_CGONAK   (0x1UL << 10U) /*!< 0x00000400 */
#define OTG_FS_DCTL_CGINAK   (0x1UL << 8U) /*!< 0x00000100 */
#define OTG_FS_DSTS_FNSOF   (0x3FFFUL << 8U) /*!< 0x003FFF00 */
#define OTG_FS_DIEPMSK_XFRCM   (0x1UL << 0U) /*!< 0x00000001 */
#define OTG_FS_DIEPMSK_EPDM   (0x1UL << 1U) /*!< 0x00000002 */
#define OTG_FS_DIEPMSK_INEPNEM   (0x1UL << 6U) /*!< 0x00000040 */
#define OTG_FS_DOEPMSK_XFRCM   (0x1UL << 0U) /*!< 0x00000001 */
#define OTG_FS_DOEPMSK_EPDM   (0x1UL << 1U) /*!< 0x00000002 */
#define OTG_FS_DOEPMSK_STUPM   (0x1UL << 3U) /*!< 0x00000008 */
#define OTG_FS_DIEPCTLx_USBAEP   (0x1UL << 15U) /*!< 0x00008000 */
#define OTG_FS_DIEPCTLx_EONUM_DPID   (0x1UL << 16U) /*!< 0x00010000 */
#define OTG_FS_DIEPCTLx_EPTYP   (0x3UL << 18U) /*!< 0x000C0000 */
#define OTG_FS_DIEPCTLx_TXFNUM   (0xFUL << 22U) /*!< 0x03C00000 */
#define OTG_FS_DIEPCTLx_CNAK   (0x1UL << 26U) /*!< 0x04000000 */
#define OTG_FS_DIEPCTLx_SNAK   (0x1UL << 27U) /*!< 0x08000000 */
#define OTG_FS_DIEPCTLx_SD0PID   (0x1UL << 28U) /*!< 0x10000000 */
#define OTG_FS_DIEPCTLx_SODDFRM   (0x1UL << 29U) /*!< 0x20000000 */
#define OTG_FS_DIEPCTLx_EPDIS   (0x1UL << 30U) /*!< 0x40000000 */
#define OTG_FS_DIEPCTLx_EPENA   (0x1UL << 31U) /*!< 0x80000000 */
#define OTG_FS_DOEPCTLx_USBAEP   (0x1UL << 15U) /*!< 0x00008000 */
#define OTG_FS_DOEPCTLx_EPTYP   (0x3UL << 18U) /*!< 0x000C0000 */
#define OTG_FS_DOEPCTLx_CNAK   (0x1UL << 26U) /*!< 0x04000000 */
#define OTG_FS_DOEPCTLx_SNAK   (0x1UL << 27U) /*!< 0x08000000 */
#define OTG_FS_DOEPCTLx_SD0PID   (0x1UL << 28U) /*!< 0x10000000 */
#define OTG_FS_DOEPCTLx_SODDFRM   (0x1UL << 29U) /*!< 0x20000000 */
#define OTG_FS_DOEPCTLx_EPDIS   (0x1UL << 30U) /*!< 0x40000000 */
#define OTG_FS_DOEPCTLx_EPENA   (0x1UL << 31U) /*!< 0x80000000 */
#define OTG_FS_DIEPINTx_XFRC   (0x1UL << 0U) /*!< 0x00000001 */
#define OTG_FS_DIEPINTx_EPDISD   (0x1UL << 1U) /*!< 0x00000002 */
#define OTG_FS_DIEPINTx_TOC   (0x1UL << 3U) /*!< 0x00000008 */
#define OTG_FS_DIEPINTx_ITTXFE   (0x1UL << 4U) /*!< 0x00000010 */
#define OTG_FS_DIEPINTx_INEPNE   (0x1UL << 6U) /*!< 0x00000040 */
#define OTG_FS_DIEPINTx_TXFE   (0x1UL << 7U) /*!< 0x00000080 */
#define OTG_FS_DOEPINTx_XFRC   (0x1UL << 0U) /*!< 0x00000001 */
#define OTG_FS_DOEPINTx_EPDISD   (0x1UL << 1U) /*!< 0x00000002 */
#define OTG_FS_DOEPINTx_STUP   (0x1UL << 3U) /*!< 0x00000008 */
#define OTG_FS_DOEPINTx_OTEPDIS   (0x1UL << 4U) /*!< 0x00000010 */
#define OTG_FS_DOEPINTx_B2BSTUP   (0x1UL << 6U) /*!< 0x00000040 */
#define OTG_FS_DIEPTSIZx_XFRSIZ   (0x7FFFFUL << 0U) /*!< 0x0007FFFF */
#define OTG_FS_DIEPTSIZx_PKTCNT   (0x3FFUL << 19U) /*!< 0x1FF80000 */
#define OTG_FS_DIEPTSIZx_MCNT   (0x3UL << 29U) /*!< 0x60000000 */
#define OTG_FS_DOEPTSIZx_XFRSIZ   (0x7FFFFUL << 0U) /*!< 0x0007FFFF */
#define OTG_FS_DOEPTSIZx_PKTCNT   (0x3FFUL << 19U) /*!< 0x1FF80000 */
#define OTG_FS_DOEPTSIZ0_STUPCNT   (0x3UL << 29U) /*!< 0x60000000 */
#define OTG_FS_DIEPTXFx_INEPTXFD   (0xFFFFUL << 16U) /*!< 0xFFFF0000 */
#define OTG_FS_GAHBCFG_GINTMSK        (0x1UL << 0U)
#define OTG_FS_DCFG_DAD_MSK           (0x7FUL << 4U)
#define OTG_FS_DCFG_DSPD_MSK          OTG_FS_DCFG_DSPD
#define OTG_FS_DSTS_FNSOF_MSK         OTG_FS_DSTS_FNSOF
#define OTG_FS_GUSBCFG_TRDT_MSK       (0xFUL << 10U)
#define OTG_FS_GRSTCTL_TXFNUM_MSK     (0x1FUL << 6U)
#define OTG_FS_DIEPCTLx_EPTYP_MSK     OTG_FS_DIEPCTLx_EPTYP
#define OTG_FS_DOEPCTLx_EPTYP_MSK     OTG_FS_DOEPCTLx_EPTYP
#define OTG_FS_DIEPCTLx_EONUM_POS     (16U)
#define OTG_FS_DOEPCTLx_EONUM_POS     (16U)
#define OTG_FS_DOEPTSIZx_PKTCNT_MSK   OTG_FS_DOEPTSIZx_PKTCNT
#define OTG_FS_DOEPTSIZx_RXDPID_MSK   (0x3UL << OTG_FS_DOEPTSIZ0_STUPCNT_POS)

/* alternate names used by USBD_FS_STM32F4xx.c */
#define OTG_FS_DIEPCTLx_SEVNFRM       OTG_FS_DIEPCTLx_SD0PID
#define OTG_FS_DOEPCTLx_SEVNFRM       OTG_FS_DOEPCTLx_SD0PID
#define OTG_FS_DIEPCTLx_STALL         (0x1UL << 21U)
#define OTG_FS_DOEPCTLx_STALL         (0x1UL << 21U)
#define OTG_FS_DIEPINTx_XFCR          OTG_FS_DIEPINTx_XFRC
#define OTG_FS_DOEPINTx_XFCR          OTG_FS_DOEPINTx_XFRC
#define OTG_FS_PCGCCTL_STPPCLK        (0x1UL << 0U)

/* bit positions */
#define OTG_FS_GUSBCFG_TRDT_POS   (10U)
#define OTG_FS_GRSTCTL_TXFNUM_POS   (6U)
#define OTG_FS_DSTS_FNSOF_POS   (8U)
#define OTG_FS_DCFG_DAD_POS   (4U)
#define OTG_FS_DIEPCTLx_EPTYP_POS   (18U)
#define OTG_FS_DIEPCTLx_TXFNUM_POS   (22U)
#define OTG_FS_DOEPCTLx_EPTYP_POS   (18U)
#define OTG_FS_DIEPTSIZx_PKTCNT_POS   (19U)
#define OTG_FS_DIEPTSIZx_MCNT_POS   (29U)
#define OTG_FS_DOEPTSIZx_PKTCNT_POS   (19U)
#define OTG_FS_DOEPTSIZ0_STUPCNT_POS   (29U)
#define OTG_FS_DIEPTXFx_INEPTXFD_POS   (16U)

/* helper macros */
#define OTG_FS_DAINT_IEPINT(n)    (1UL << (n))
#define OTG_FS_DAINT_OEPINT(n)    (1UL << ((n) + 16U))
#define OTG_FS_GUSBCFG_TRDT(n)    (((n) & 0xFU) << OTG_FS_GUSBCFG_TRDT_POS)
#define OTG_FS_GRSTCTL_TXFNUM(n)  (((n) & 0xFU) << OTG_FS_GRSTCTL_TXFNUM_POS)
#define OTG_FS_DCFG_DAD(n)        (((n) & 0x7FU) << OTG_FS_DCFG_DAD_POS)

#endif /* OTG_FS_STM32F4xx_H_ */
