/* 
 * @author wangxiaoran
 * @brief set syscall filter
 * @details use a wrapper function to filter syscall
 *  white list 
 */
#include "syscall_white_list.h"

// syscall is valid or not
enum syscall_status {
  INVALID = -1,
  VALID = 0,
};


int check_syscall(unsigned long nbr, uintptr_t parm1, uintptr_t parm2,
                          uintptr_t parm3, uintptr_t parm4, uintptr_t parm5,
                          uintptr_t parm6) 
{    
  uintptr_t params[] = {
    parm1,
    parm2,
    parm3,
    parm4,
    parm5,
    parm6,
  };

  int len = sizeof(white_list)/sizeof(struct syscall_filter_nbr);
  struct syscall_filter_nbr syscall_item;

  for (int i = 0; i < len; i++) {
    syscall_item = white_list[i];
    
    // check nbr and filter params
    if (syscall_item.nbr == nbr) {
      int params_to_check = syscall_item.param_num > 6 ? 6 : syscall_item.param_num;

      // check single param 
      for (int param_ptr = 0; param_ptr < params_to_check; param_ptr++) {
        struct syscall_param_filter ptr = syscall_item.filter[param_ptr];

				int restriction_len = ptr.tocheck > 2 ? ptr.tocheck : 2;
        // check restrictions
        for (int restriction_to_check = 0; restriction_to_check < restriction_len; restriction_to_check++) {
					struct syscall_param_filter_item restriction = ptr.restrictions[restriction_to_check];
          
					switch (restriction.op)
          {
          case EQ: {
            if (params[param_ptr] != restriction.val) {
              return INVALID;
            }
            break;
          }
          case LESS_THAN: {
            if (params[param_ptr] >= restriction.val) {
              return INVALID;
            }
            break;
          }
          case MORE_THAN: {
            if (params[param_ptr] <= restriction.val) {
              return INVALID;
            }
            break;
          } 
          default:
            break;
          }
        }
      }
    }
  }

  return VALID;
}
