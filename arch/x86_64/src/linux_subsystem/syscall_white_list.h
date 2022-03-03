typedef unsigned long int	uintptr_t;

// syscall filter operations
enum syscall_param_filter_op {
  PASS,
  FAIL,
  EQ,
  NEQ,
  LESS_THAN,
  MORE_THAN,
};

// to restrict the valid region of syscall params
struct syscall_param_filter_item
{
  enum syscall_param_filter_op op;
  uintptr_t val;
};

// each param has two restrictions, tocheck determin how many restrictions
struct syscall_param_filter
{
  int tocheck;
  struct syscall_param_filter_item restrictions[2];
};

// check params of nbr
struct syscall_filter_nbr
{
  unsigned long nbr;
  int param_num;
  struct syscall_param_filter filter[6];
};

struct syscall_filter_nbr white_list[] = {
  {
    .nbr = 231,
    .param_num = 1,
    .filter = {
       {
        .tocheck = 1,
        .restrictions = {
          {
            .op = NEQ,
            .val = 521
          },
        }
      },
    }
  },
};
