// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
  *
 * Copyright (C) 2015 XSky <haomai@xsky.com>
 *
 * Author: Haomai Wang <haomaiwang@gmail.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include <unistd.h>

#include "KernelDevice.h"
#if defined(HAVE_SPDK)
#include "NVMEDevice.h"
#endif

#include "common/debug.h"

#define dout_subsys ceph_subsys_bdev
#undef dout_prefix
#define dout_prefix *_dout << "bdev "

void IOContext::aio_wait()
{
  Mutex::Locker l(lock);
  // see _aio_thread for waker logic
  num_waiting.inc();
  while (num_running.read() > 0 || num_reading.read() > 0) {
    dout(10) << __func__ << " " << this
	     << " waiting for " << num_running.read() << " aios and/or "
	     << num_reading.read() << " readers to complete" << dendl;
    cond.Wait(lock);
  }
  num_waiting.dec();
  dout(20) << __func__ << " " << this << " done" << dendl;
}

BlockDevice *BlockDevice::create(const string& path, aio_callback_t cb, void *cbpriv)
{
  string type = "kernel";
  char buf[10];
  int r = ::readlink(path.c_str(), buf, sizeof(buf));
  if (r >= 0 && strncmp(buf, SPDK_PREFIX, sizeof(SPDK_PREFIX)-1) == 0) {
    type = "ust-nvme";
  }
  dout(1) << __func__ << " path " << path << " type " << type << dendl;

  if (type == "kernel") {
    return new KernelDevice(cb, cbpriv);
  }
#if defined(HAVE_SPDK)
  if (type == "ust-nvme") {
    return new NVMEDevice(cb, cbpriv);
  }
#endif

  derr << __func__ << " unknown backend " << type << dendl;
  assert(0);
  return NULL;
}

void BlockDevice::queue_reap_ioc(IOContext *ioc)
{
  Mutex::Locker l(ioc_reap_lock);
  if (ioc_reap_count.read() == 0)
    ioc_reap_count.inc();
  ioc_reap_queue.push_back(ioc);
}

void BlockDevice::reap_ioc()
{
  if (ioc_reap_count.read()) {
    Mutex::Locker l(ioc_reap_lock);
    for (auto p : ioc_reap_queue) {
      dout(20) << __func__ << " reap ioc " << p << dendl;
      delete p;
    }
    ioc_reap_queue.clear();
    ioc_reap_count.dec();
  }
}
