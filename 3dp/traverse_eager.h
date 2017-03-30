#ifndef traverse_eager_h
#define traverse_eager_h
#include "exafmm.h"

namespace exafmm {
  //! Recursive call to post-order tree traversal for upward pass
  void upwardPass(Cell * Ci) {
    for (Cell * Cj=Ci->CHILD; Cj!=Ci->CHILD+Ci->NCHILD; Cj++) { // Loop over child cells
#pragma omp task untied if(Cj->NBODY > 100)                     //  Start OpenMP task if large enough task
      upwardPass(Cj);                                           //  Recursive call for child cell
    }                                                           // End loop over child cells
#pragma omp taskwait                                            // Synchronize OpenMP tasks
    Ci->M.resize(NTERM, 0.0);                                   // Allocate and initialize multipole coefs
    Ci->L.resize(NTERM, 0.0);                                   // Allocate and initialize local coefs
    if(Ci->NCHILD==0) P2M(Ci);                                  // P2M kernel
    M2M(Ci);                                                    // M2M kernel
  }

  //! Upward pass interface
  void upwardPass(Cells & cells) {
#pragma omp parallel                                            // Start OpenMP
#pragma omp single nowait                                       // Start OpenMP single region with nowait
    upwardPass(&cells[0]);                                      // Pass root cell to recursive call
  }

  //! Recursive call to dual tree traversal for horizontal pass
  void horizontalPass(Cell * Ci, Cell * Cj) {
    for (int d=0; d<3; d++) dX[d] = Ci->X[d] - Cj->X[d] - Xperiodic[d];// Distance vector from source to target
    real_t R2 = norm(dX) * theta * theta;                       // Scalar distance squared
    if (R2 > (Ci->R + Cj->R) * (Ci->R + Cj->R)) {               // If distance is far enough
      M2L(Ci, Cj);                                              //  M2L kernel
    } else if (Ci->NCHILD == 0 && Cj->NCHILD == 0) {            // Else if both cells are leafs
      P2P(Ci, Cj);                                              //  P2P kernel
    } else if (Cj->NCHILD == 0 || (Ci->R >= Cj->R && Ci->NCHILD != 0)) {// If Cj is leaf or Ci is larger
      for (Cell * ci=Ci->CHILD; ci!=Ci->CHILD+Ci->NCHILD; ci++) {// Loop over Ci's children
        horizontalPass(ci, Cj);                                 //   Recursive call to target child cells
      }                                                         //  End loop over Ci's children
    } else {                                                    // Else if Ci is leaf or Cj is larger
      for (Cell * cj=Cj->CHILD; cj!=Cj->CHILD+Cj->NCHILD; cj++) {// Loop over Cj's children
        horizontalPass(Ci, cj);                                 //   Recursive call to source child cells
      }                                                         //  End loop over Cj's children
    }                                                           // End if for leafs and Ci Cj size
  }

  //! Horizontal pass for periodic images
  void periodic(Cell * Ci0, Cell * Cj0, real_t cycle) {
    Cells pcells(27);                                           // Create cells
    for (size_t c=0; c<pcells.size(); c++) {                    // Loop over periodic cells
      pcells[c].M.resize(NTERM, 0.0);                           //  Allocate & initialize M coefs
      pcells[c].L.resize(NTERM, 0.0);                           //  Allocate & initialize L coefs
    }                                                           // End loop over periodic cells
    Cell * Ci = &pcells.back();                                 // Last cell is periodic parent cell
    *Ci = *Cj0;                                                 // Copy values from source root
    Ci->CHILD = &pcells[0];                                     // Pointer of first periodic child cell
    Ci->NCHILD = 26;                                            // Number of periodic child cells
    for (int level=0; level<images-1; level++) {                // Loop over sublevels of tree
      for (int ix=-1; ix<=1; ix++) {                            //  Loop over x periodic direction
        for (int iy=-1; iy<=1; iy++) {                          //   Loop over y periodic direction
          for (int iz=-1; iz<=1; iz++) {                        //    Loop over z periodic direction
            if (ix != 0 || iy != 0 || iz != 0) {                //     If periodic cell is not at center
              for (int cx=-1; cx<=1; cx++) {                    //      Loop over x periodic direction (child)
                for (int cy=-1; cy<=1; cy++) {                  //       Loop over y periodic direction (child)
                  for (int cz=-1; cz<=1; cz++) {                //        Loop over z periodic direction (child)
                    Xperiodic[0] = (ix * 3 + cx) * cycle;       //   Coordinate offset for x periodic direction
                    Xperiodic[1] = (iy * 3 + cy) * cycle;       //   Coordinate offset for y periodic direction
                    Xperiodic[2] = (iz * 3 + cz) * cycle;       //   Coordinate offset for z periodic direction
                    M2L(Ci0, Ci);                               //         M2L kernel
                  }                                             //        End loop over z periodic direction (child)
                }                                               //       End loop over y periodic direction (child)
              }                                                 //      End loop over x periodic direction (child)
            }                                                   //     Endif for periodic center cell
          }                                                     //    End loop over z periodic direction
        }                                                       //   End loop over y periodic direction
      }                                                         //  End loop over x periodic direction
      Cell * Cj = &pcells[0];                                   //  Iterator of periodic neighbor cells
      for (int ix=-1; ix<=1; ix++) {                            //  Loop over x periodic direction
        for (int iy=-1; iy<=1; iy++) {                          //   Loop over y periodic direction
          for (int iz=-1; iz<=1; iz++) {                        //    Loop over z periodic direction
            if (ix != 0 || iy != 0 || iz != 0) {                //     If periodic cell is not at center
              Cj->X[0] = Ci->X[0] + ix * cycle;                 //      Set new x coordinate for periodic image
              Cj->X[1] = Ci->X[1] + iy * cycle;                 //      Set new y cooridnate for periodic image
              Cj->X[2] = Ci->X[2] + iz * cycle;                 //      Set new z coordinate for periodic image
              Cj->M = Ci->M;                                    //      Copy multipoles to new periodic image
              Cj++;                                             //      Increment periodic cell iterator
            }                                                   //     Endif for periodic center cell
          }                                                     //    End loop over z periodic direction
        }                                                       //   End loop over y periodic direction
      }                                                         //  End loop over x periodic direction
      M2M(Ci);                                                  //  Evaluate periodic M2M kernels for this sublevel
      cycle *= 3;                                               //  Increase periodic cycle by number of neighbors
    }                                                           // End loop over sublevels of tree
  }

  //! Horizontal pass interface
  void horizontalPass(Cells & icells, Cells & jcells) {
    if (images == 0) {                                          // If non-periodic boundary condition
      horizontalPass(&icells[0], &jcells[0]);                   //  Pass root cell to recursive call
    } else {                                                    // If periodic boundary condition
      for (int ix=-1; ix<=1; ix++) {                            //  Loop over x periodic direction
        for (int iy=-1; iy<=1; iy++) {                          //   Loop over y periodic direction
          for (int iz=-1; iz<=1; iz++) {                        //    Loop over z periodic direction
            Xperiodic[0] = ix * cycle;                          //     Coordinate offset for x periodic direction
            Xperiodic[1] = iy * cycle;                          //     Coordinate offset for y periodic direction
            Xperiodic[2] = iz * cycle;                          //     Coordinate offset for z periodic direction
            horizontalPass(&icells[0], &jcells[0]);             //     Horizontal pass for this periodic image
          }                                                     //    End loop over z periodic direction
        }                                                       //   End loop over y periodic direction
      }                                                         //  End loop over x periodic direction
      periodic(&icells[0], &jcells[0], cycle);                  //  Horizontal pass for periodic images
    }                                                           // End if for periodic boundary condition
  }

  //! Recursive call to pre-order tree traversal for downward pass
  void downwardPass(Cell * Cj) {
    L2L(Cj);                                                    // L2L kernel
    if (Cj->NCHILD==0) L2P(Cj);                                 // L2P kernel
    for (Cell * Ci=Cj->CHILD; Ci!=Cj->CHILD+Cj->NCHILD; Ci++) { // Loop over child cells
#pragma omp task untied if(Ci->NBODY > 100)                     //  Start OpenMP task if large enough task
      downwardPass(Ci);                                         //  Recursive call for child cell
    }                                                           // End loop over chlid cells
#pragma omp taskwait                                            // Synchronize OpenMP tasks
  }

  //! Downward pass interface
  void downwardPass(Cells & cells) {
#pragma omp parallel                                            // Start OpenMP
#pragma omp single nowait                                       // Start OpenMP single region with nowait
    downwardPass(&cells[0]);                                    // Pass root cell to recursive call
  }

  //! Direct summation
  void direct(Bodies & bodies, Bodies & jbodies) {
    Cells cells(2);                                             // Define a pair of cells to pass to P2P kernel
    Cell * Ci = &cells[0];                                      // Allocate single target
    Cell * Cj = &cells[1];                                      // Allocate single source
    for (int ix=-1; ix<=1; ix++) {                              //  Loop over x periodic direction
      for (int iy=-1; iy<=1; iy++) {                            //   Loop over y periodic direction
        for (int iz=-1; iz<=1; iz++) {                          //    Loop over z periodic direction
          Xperiodic[0] = ix * cycle;                            //     Coordinate offset for x periodic direction
          Xperiodic[1] = iy * cycle;                            //     Coordinate offset for y periodic direction
          Xperiodic[2] = iz * cycle;                            //     Coordinate offset for z periodic direction
          Ci->BODY = &bodies[0];                                //     Iterator of first target body
          Ci->NBODY = bodies.size();                            //     Number of target bodies
          Cj->BODY = &jbodies[0];                               //     Iterator of first source body
          Cj->NBODY = jbodies.size();                           //     Number of source bodies
          P2P(Ci, Cj);                                          //     Evaluate P2P kenrel
        }                                                       //    End loop over z periodic direction
      }                                                         //   End loop over y periodic direction
    }                                                           //  End loop over x periodic direction
  }
}
#endif
