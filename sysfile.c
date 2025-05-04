int
sys_read(void)
{
  struct file *f;
  int n;
  char *p;

  readcount++;  // 在函数开始就增加计数
  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return fileread(f, p, n);
} 