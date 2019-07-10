#pragma once
// xkvisor constrol structure

// read only struct
struct XCS {
  // values are ppn leased to guest os by xkvisor
  int P2M[num_app_pages];

}

