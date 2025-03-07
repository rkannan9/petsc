static char help[] = "Example program demonstrating projection between particle and finite element spaces\n\n";

#include <petscdmplex.h>
#include <petscds.h>
#include <petscdmswarm.h>
#include <petscksp.h>

int main(int argc, char **argv)
{
  DM             dm, sw;
  PetscFE        fe;
  Vec            u_f;
  DMPolytopeType ct;
  PetscInt       dim, Nc = 1, faces[3];
  PetscInt       Np = 10, field = 0, zero = 0, bs, cStart;
  PetscReal      energy_0 = 0, energy_1 = 0;
  PetscReal      lo[3], hi[3], h[3];
  PetscBool      removePoints = PETSC_TRUE;
  PetscReal     *wq, *coords;
  PetscDataType  dtype;

  PetscFunctionBeginUser;
  PetscCall(PetscInitialize(&argc, &argv, NULL, help));
  /* Create a mesh */
  PetscCall(DMCreate(PETSC_COMM_WORLD, &dm));
  PetscCall(DMSetType(dm, DMPLEX));
  PetscCall(DMSetFromOptions(dm));
  PetscCall(DMViewFromOptions(dm, NULL, "-dm_view"));

  PetscCall(DMGetDimension(dm, &dim));
  bs = dim;
  PetscCall(PetscOptionsGetIntArray(NULL, NULL, "-dm_plex_box_faces", faces, &bs, NULL));
  PetscCall(PetscOptionsGetInt(NULL, NULL, "-np", &Np, NULL));
  PetscCall(DMGetBoundingBox(dm, lo, hi));
  for (PetscInt i = 0; i < dim; ++i) {
    h[i] = (hi[i] - lo[i]) / faces[i];
    PetscCall(PetscPrintf(PETSC_COMM_SELF, " lo = %g hi = %g n = %" PetscInt_FMT " h = %g\n", (double)lo[i], (double)hi[i], faces[i], (double)h[i]));
  }
  // Create FE space
  PetscCall(DMPlexGetHeightStratum(dm, 0, &cStart, NULL));
  PetscCall(DMPlexGetCellType(dm, cStart, &ct));
  PetscCall(PetscFECreateByCell(PETSC_COMM_SELF, dim, Nc, ct, NULL, PETSC_DECIDE, &fe));
  PetscCall(PetscFESetFromOptions(fe));
  PetscCall(PetscObjectSetName((PetscObject)fe, "fe"));
  PetscCall(DMSetField(dm, field, NULL, (PetscObject)fe));
  PetscCall(DMCreateDS(dm));
  PetscCall(PetscFEDestroy(&fe));
  PetscCall(DMCreateGlobalVector(dm, &u_f));
  // Create particle swarm
  PetscCall(DMCreate(PETSC_COMM_SELF, &sw));
  PetscCall(DMSetType(sw, DMSWARM));
  PetscCall(DMSetDimension(sw, dim));
  PetscCall(DMSwarmSetType(sw, DMSWARM_PIC));
  PetscCall(DMSwarmSetCellDM(sw, dm));
  PetscCall(DMSwarmRegisterPetscDatatypeField(sw, "w_q", Nc, PETSC_SCALAR));
  PetscCall(DMSwarmFinalizeFieldRegister(sw));
  PetscCall(DMSwarmSetLocalSizes(sw, Np, zero));
  PetscCall(DMSetFromOptions(sw));
  PetscCall(DMSwarmGetField(sw, "w_q", &bs, &dtype, (void **)&wq));
  PetscCall(DMSwarmGetField(sw, "DMSwarmPIC_coor", &bs, &dtype, (void **)&coords));
  for (PetscInt p = 0; p < Np; ++p) {
    coords[p * 2 + 0] = -PetscCosReal((PetscReal)(p + 1) / (PetscReal)(Np + 1) * PETSC_PI);
    coords[p * 2 + 1] = PetscSinReal((PetscReal)(p + 1) / (PetscReal)(Np + 1) * PETSC_PI);
    wq[p]             = 1.0;
    energy_0 += wq[p] * (PetscSqr(coords[p * 2 + 0]) + PetscSqr(coords[p * 2 + 1]));
  }
  PetscCall(DMSwarmRestoreField(sw, "DMSwarmPIC_coor", &bs, &dtype, (void **)&coords));
  PetscCall(DMSwarmRestoreField(sw, "w_q", &bs, &dtype, (void **)&wq));
  PetscCall(DMSwarmMigrate(sw, removePoints));
  PetscCall(PetscObjectSetName((PetscObject)sw, "Particle Grid"));
  PetscCall(DMViewFromOptions(sw, NULL, "-swarm_view"));

  // Project between particles and continuum field
  const char *fieldnames[1] = {"w_q"};
  Vec         fields[1]     = {u_f};
  PetscCall(DMSwarmProjectFields(sw, 1, fieldnames, fields, SCATTER_FORWARD));
  PetscCall(DMSwarmProjectFields(sw, 1, fieldnames, fields, SCATTER_REVERSE));

  // Compute energy
  PetscCall(DMSwarmGetField(sw, "w_q", &bs, &dtype, (void **)&wq));
  PetscCall(DMSwarmGetField(sw, "DMSwarmPIC_coor", &bs, &dtype, (void **)&coords));
  for (PetscInt p = 0; p < Np; ++p) energy_1 += wq[p] * (PetscSqr(coords[p * 2 + 0]) + PetscSqr(coords[p * 2 + 1]));
  PetscCall(DMSwarmRestoreField(sw, "DMSwarmPIC_coor", &bs, &dtype, (void **)&coords));
  PetscCall(DMSwarmRestoreField(sw, "w_q", &bs, &dtype, (void **)&wq));
  PetscCall(PetscPrintf(PETSC_COMM_SELF, "Energy = %20.12e error = %20.12e\n", (double)energy_0, (double)((energy_1 - energy_0) / energy_0)));

  // Cleanup
  PetscCall(VecDestroy(&u_f));
  PetscCall(DMDestroy(&sw));
  PetscCall(DMDestroy(&dm));
  PetscCall(PetscFinalize());
  return 0;
}

/*TEST

  build:
    requires: !complex

  test:
    suffix: 0
    requires: double triangle
    args: -dm_plex_simplex 0 -dm_plex_box_faces 4,2 -dm_plex_box_lower -2.0,0.0 -dm_plex_box_upper 2.0,2.0 \
           -np 50 -petscspace_degree 2 \
           -ptof_ksp_type cg -ptof_pc_type ilu -ptof_ksp_rtol 1.e-14 \
           -ftop_ksp_type lsqr -ftop_pc_type none -ftop_ksp_rtol 1.e-14 \
           -dm_view -swarm_view
    filter: grep -v DM_ | grep -v atomic

  test:
    suffix: bjacobi
    requires: double triangle
    args: -dm_plex_simplex 0 -dm_plex_box_faces 4,2 -dm_plex_box_lower -2.0,0.0 -dm_plex_box_upper 2.0,2.0 \
          -np 50 -petscspace_degree 2 -dm_plex_hash_location \
          -ptof_ksp_type cg -ptof_pc_type ilu -ptof_ksp_rtol 1.e-14 \
          -ftop_ksp_type lsqr -ftop_pc_type bjacobi -ftop_sub_pc_type lu -ftop_sub_pc_factor_shift_type nonzero \
          -dm_view -swarm_view -ftop_ksp_rtol 1.e-14
    filter: grep -v DM_ | grep -v atomic

TEST*/
