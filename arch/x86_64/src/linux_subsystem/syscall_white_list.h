
#define SYS_read	 0
#define SYS_write	 1
#define SYS_open   2
#define SYS_close  3


typedef unsigned long int	uintptr_t;

// syscall filter operations
enum syscall_param_filter_op {
  PASS,
  FAIL,
  EQ,
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
    .nbr = SYS_read,
    .param_num = 2,
    .filter = {
       {
        .tocheck = 2,
        .restrictions = {
          {
            .op = LESS_THAN,
            .val = 2
          },
          {
            .op = MORE_THAN,
            .val = 0
          },
        }
      },
			{
        .tocheck = 2,
        .restrictions = {
          {
            .op = LESS_THAN,
            .val = 5
          },
          {
            .op = MORE_THAN,
            .val = 3
          },
        }
      },
    }
  },
  {
    .nbr = SYS_write,
    .param_num = 1,
    .filter = {
      {
        .tocheck = 1,
        .restrictions = {
          {
            .op = EQ,
            .val = 2
          }
        }
      }
    }
  },
  {
    .nbr = SYS_open,
    .param_num = 1,
    .filter = {
      {
        .tocheck = 1,
        .restrictions = {
          {
            .op = EQ,
            .val = 2
          }
        }
      }
    }
  },
  {
    .nbr = SYS_close,
    .param_num = 1,
    .filter = {
      {
        .tocheck = 1,
        .restrictions = {
          {
            .op = EQ,
            .val = 2
          }
        }
      }
    }
  },
};
