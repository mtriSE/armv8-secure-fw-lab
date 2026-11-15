/* EL1 exception vector table of the payload (A4). Same generator as EL3,
 * with the EL1 registers: an exception taken at EL1 saves state in
 * ELR_EL1/SPSR_EL1, not in the EL3 registers. */
#include "vector_table.h"

    vector_table el1_vectors, el1, el1_exception_handler
