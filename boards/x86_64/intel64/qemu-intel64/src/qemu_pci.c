/*
 * boards/x86_64/intel64/qemu-intel64/src/qemu_pci.c
 *
 * Copyright (c) ChungFan Yang @ Fixstars Corporation, 2020
 *                               <chungfan.yang@fixstars.com>
 *
 * The MSI and MSI-X vector setup function are taken from Jailhouse inmate
 * library
 *
 * Copyright (c) Siemens AG, 2014
 *
 * Authors:
 *  Jan Kiszka <jan.kiszka@siemens.com>
 *
 * This work is licensed under the terms of the BSD license.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <string.h>

#include <nuttx/pci/pci.h>
#include <nuttx/mm/gran.h>

#include "qemu_pci_readwrite.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Functions Definitions
 ****************************************************************************/

static int qemu_pci_cfg_write(FAR struct pci_dev_s *dev, uintptr_t addr,
                              uint32_t val, unsigned int size);

static uint32_t qemu_pci_cfg_read(FAR struct pci_dev_s *dev, uintptr_t addr,
                                  unsigned int size);

static void *qemu_pci_map_mem(FAR struct pci_dev_s *dev, uintptr_t addr,
                              unsigned long length);

static int qemu_pci_msix_register(FAR struct pci_dev_s *dev,
                                  uint32_t vector, uint32_t index);

static int qemu_pci_msi_register(FAR struct pci_dev_s *dev,
                                 uint16_t vector);

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_QEMU_PCI_BAR_PAGE_COUNT

static volatile uint8_t \
           qemu_pci_bar_pages[CONFIG_QEMU_PCI_BAR_PAGE_COUNT * PAGE_SIZE] \
           __attribute__((aligned(PAGE_SIZE))) \
           __attribute__((section(".pcibar")));

static
uint64_t qemu_bar_pages_bitmap[(CONFIG_QEMU_PCI_BAR_PAGE_COUNT + 63) / 64];

#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

struct pci_bus_ops_s qemu_pci_bus_ops =
{
    .pci_cfg_write     =   qemu_pci_cfg_write,
    .pci_cfg_read      =   qemu_pci_cfg_read,
    .pci_map_mem       =   qemu_pci_map_mem,
    .pci_msix_register =   qemu_pci_msix_register,
    .pci_msi_register  =   qemu_pci_msi_register,
};

struct pci_bus_s qemu_pci_bus =
{
    .ops = &qemu_pci_bus_ops,
};

struct pci_bus_s *pci_bus = &qemu_pci_bus;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_QEMU_PCI_BAR_PAGE_COUNT

/****************************************************************************
 * Name: qemu_page_alloc
 *
 * Description:
 *  Allocate pages from the bar pages memory reions
 *
 * Input Parameters:
 *   size   - The number of bytes to be allocated, must be page aligned
 *
 * Returned Value:
 *   0: success, <0: A negated errno
 *
 ****************************************************************************/

void *qemu_page_alloc(size_t size)
{
  irqstate_t flags;
  unsigned long remain;
  void *ret = NULL;
  unsigned long start_idx = 0;

  if (size == 0)
      return ret;

  size = (size + (PAGE_SIZE - 1)) & PAGE_MASK;

  remain = size / PAGE_SIZE;

  flags = enter_critical_section();

  for (int j = 0; (j < CONFIG_QEMU_PCI_BAR_PAGE_COUNT) && (remain > 0); j++)
    {
      if (!((qemu_bar_pages_bitmap[j / 64] >> (j & 0x3f)) & 0x1))
          remain--;
      else
          remain = size / PAGE_SIZE;

      if (remain == 0)
        {
          start_idx = j - (size / PAGE_SIZE - 1);
          break;
        }
    }

  if (!remain)
    {
      remain = size / PAGE_SIZE;
      for (int i = start_idx; i < start_idx + remain; i++)
        {
          qemu_bar_pages_bitmap[i / 64] |= (1 << (i & 0x3f));
        }

      ret = (void *)qemu_pci_bar_pages + start_idx * PAGE_SIZE;
    }

  leave_critical_section(flags);

  return ret;
}

/****************************************************************************
 * Name: qemu_page_free
 *
 * Description:
 *  Free pages from the bar pages memory regions
 *
 * Input Parameters:
 *   size   - The number of bytes to be allocated, must be page aligned
 *
 * Returned Value:
 *   0: success, <0: A negated errno
 *
 ****************************************************************************/

void qemu_page_free(void *ptr, size_t size)
{
  irqstate_t flags;
  unsigned long start_idx =
    ((uintptr_t)ptr - (uintptr_t)qemu_pci_bar_pages) / PAGE_SIZE;
  unsigned long remain = size / PAGE_SIZE;

  if (size & ~PAGE_MASK || size == 0 || !ptr)
    return;

  flags = enter_critical_section();

  for (int i = start_idx; i < start_idx + remain; i++)
    {
      qemu_bar_pages_bitmap[i / 64] &= ~(1 << (i & 0x3f));
    }

  leave_critical_section(flags);

  return;
}

#endif

/****************************************************************************
 * Name: qemu_pci_cfg_write
 *
 * Description:
 *  Write 8, 16, 32 bits data to PCI configuration space of device
 *  specified by dev
 *
 * Input Parameters:
 *   bdf    - Device private data
 *   val    - Value to be written
 *   size   - The number of bytes to be written
 *
 * Returned Value:
 *   0: success, <0: A negated errno
 *
 ****************************************************************************/

static int qemu_pci_cfg_write(FAR struct pci_dev_s *dev, uintptr_t addr,
                              uint32_t val, unsigned int size)
{
  DEBUGASSERT(size == 1 || size == 2 || size == 4);

  return __qemu_pci_cfg_write(dev->bdf, addr, val, size);
}

/****************************************************************************
 * Name: qemu_pci_cfg_read
 *
 * Description:
 *  Read 8, 16, 32 bits data from PCI configuration space of device
 *  specified by dev
 *
 * Input Parameters:
 *   dev    - Device private data
 *   size   - The requested number of bytes to be read
 *
 * Returned Value:
 *    Value in configuration space
 *
 ****************************************************************************/

static uint32_t qemu_pci_cfg_read(FAR struct pci_dev_s *dev, uintptr_t addr,
                                  unsigned int size)
{
  DEBUGASSERT(size == 1 || size == 2 || size == 4);

  return __qemu_pci_cfg_read(dev->bdf, addr, size);
}

/****************************************************************************
 * Name: qemu_pci_map_mem
 *
 * Description:
 *  Map address in a 32 bits bar in the memory address space
 *
 * Input Parameters:
 *   dev    - Device private data
 *   addr   - Memory address
 *   length - Map length, multiple of PAGE_SIZE
 *
 * Returned Value:
 *   0: error, otherwise: bar content
 *
 ****************************************************************************/

static void *qemu_pci_map_mem(FAR struct pci_dev_s *dev, uintptr_t addr,
                              unsigned long length)
{
  if (addr && (addr < 0xffffffff))
    {
      up_map_region((void *)((uintptr_t)addr), length,
        X86_PAGE_WR | X86_PAGE_PRESENT | X86_PAGE_NOCACHE | X86_PAGE_GLOBAL);

      return (void *)((uintptr_t)addr);
    }
  else
#ifndef CONFIG_QEMU_PCI_BAR_PAGE_COUNT
    {
      return NULL;
    }
#else
    {
      uintptr_t addr_to = (uintptr_t)qemu_page_alloc(length);

      if (!addr_to)
          return NULL;

      if (!addr)
        addr = addr_to;

      up_map_region_to((void *)addr_to, (void *)addr, length,
        X86_PAGE_WR | X86_PAGE_PRESENT | X86_PAGE_NOCACHE | X86_PAGE_GLOBAL);

      return (void *)addr_to;
    }
#endif /* CONFIG_QEMU_PCI_BAR_PAGE_COUNT */
}

/****************************************************************************
 * Name: qemu_pci_msix_register
 *
 * Description:
 *  Map a device MSI-X vector to a platform IRQ vector
 *
 * Input Parameters:
 *   dev - Device
 *   vector - IRQ number of the platform
 *   index  - Device MSI-X vector number
 *
 * Returned Value:
 *   <0: Mapping failed
 *    0: Mapping succeed
 *
 ****************************************************************************/

static int qemu_pci_msix_register(FAR struct pci_dev_s *dev,
                                  uint32_t vector, uint32_t index)
{
  unsigned int bar;
  uint16_t message_control;
  uint32_t table_bar_ind;
  uint32_t table_addr_32;
  uint64_t msix_table_addr = 0;

  int cap = pci_find_cap(dev, PCI_CAP_MSIX);
  if (cap < 0)
      return -EINVAL;

  message_control = __qemu_pci_cfg_read(dev->bdf, cap + PCI_MSIX_MCR,
                                        PCI_MSIX_MCR_SIZE);

  /* bounds check */

  if (index > (message_control & PCI_MSIX_MCR_TBL_MASK))
      return -EINVAL;

  table_bar_ind = __qemu_pci_cfg_read(dev->bdf, cap + PCI_MSIX_TBL,
                                      PCI_MSIX_TBL_SIZE);

  bar = (table_bar_ind & PCI_MSIX_BIR_MASK);

  table_addr_32 = pci_get_bar(dev, bar);

  if ((table_addr_32 & PCI_BAR_64BIT) != PCI_BAR_64BIT)
    {
      /* 32 bit bar */

      msix_table_addr = table_addr_32;
    }
  else
    {
      msix_table_addr = pci_get_bar64(dev, bar);
    }

  msix_table_addr &= ~0xf;
  msix_table_addr += table_bar_ind & ~PCI_MSIX_BIR_MASK;

  /* enable and mask */

  message_control |= (PCI_MSIX_MCR_EN | PCI_MSIX_MCR_FMASK);
  __qemu_pci_cfg_write(dev->bdf, cap + PCI_MSIX_MCR,
                       message_control, PCI_MSIX_MCR_SIZE);

  msix_table_addr += PCI_MSIX_TBL_ENTRY_SIZE * index;
  mmio_write32((uint32_t *)(msix_table_addr + PCI_MSIX_TBL_LO_ADDR),
               0xfee00000 | up_apic_cpu_id() << PCI_MSIX_APIC_ID_OFFSET);
  mmio_write32((uint32_t *)(msix_table_addr + PCI_MSIX_TBL_HI_ADDR),
               0);
  mmio_write32((uint32_t *)(msix_table_addr + PCI_MSIX_TBL_MSG_DATA),
               vector);
  mmio_write32((uint32_t *)(msix_table_addr + PCI_MSIX_TBL_VEC_CTL),
               0);

  /* enable and unmask */

  message_control &= ~PCI_MSIX_MCR_FMASK;

  __qemu_pci_cfg_write(dev->bdf, cap + PCI_MSIX_MCR,
                       message_control, PCI_MSIX_MCR_SIZE);

  return 0;
}

/****************************************************************************
 * Name: qemu_pci_msi_register
 *
 * Description:
 *  Map device MSI vectors to a platform IRQ vector
 *
 * Input Parameters:
 *   dev - Device
 *   vector - IRQ number of the platform
 *
 * Returned Value:
 *   <0: Mapping failed
 *    0: Mapping succeed
 *
 ****************************************************************************/

static int qemu_pci_msi_register(FAR struct pci_dev_s *dev, uint16_t vector)
{
  uint16_t ctl;
  uint16_t data;

  int cap = pci_find_cap(dev, PCI_CAP_MSI);
  if (cap < 0)
      return -1;

  uint32_t dest = 0xfee00000 | (up_apic_cpu_id() << PCI_MSI_APIC_ID_OFFSET);
  __qemu_pci_cfg_write(dev->bdf, cap + PCI_MSI_MAR, dest, PCI_MSI_MAR_SIZE);

  ctl = __qemu_pci_cfg_read(dev->bdf, cap + PCI_MSI_MCR, PCI_MSI_MCR_SIZE);
  if ((ctl & PCI_MSI_MCR_64) == PCI_MSI_MCR_64)
    {
      uint32_t tmp = 0;
      __qemu_pci_cfg_write(dev->bdf,
                           cap + PCI_MSI_MAR64_HI, tmp,
                           PCI_MSI_MAR64_HI_SIZE);
      data = cap + PCI_MSI_MDR64;
    }
  else
    {
      data = cap + PCI_MSI_MDR;
    }

  __qemu_pci_cfg_write(dev->bdf, data, vector, PCI_MSI_MDR_SIZE);

  __qemu_pci_cfg_write(dev->bdf, cap + PCI_MSI_MCR, vector,
                       PCI_MSI_MCR_SIZE);

  __qemu_pci_cfg_write(dev->bdf, cap + PCI_MSI_MCR,
                       PCI_MSI_MCR_EN, PCI_MSI_MCR_SIZE);

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: qemu_pci_init
 *
 * Description:
 *  Initialize the PCI bus *
 *
 ****************************************************************************/

void qemu_pci_init(void)
{
#ifdef CONFIG_QEMU_PCI_BAR_PAGE_COUNT
  memset(qemu_bar_pages_bitmap, 0, sizeof(qemu_bar_pages_bitmap));
#endif

  pci_initialize(&qemu_pci_bus);
}
