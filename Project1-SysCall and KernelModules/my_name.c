#include <linux/module.h>
#include <linux/kernel.h>

int Justin_Jin_Init(void){
  printk("[Group-<27>][Justin Jin, Kyle Gilman, Aria Saboonchi Lilabady, Leo-Paul Morel] Hello, I am Justin Jin, a student of CSE330 Spring 2023\n");
  return 0;
	       }

void Justin_Jin_Exit(void){
  printk("[Group-<27>][Justin Jin, Kyle Gilman, Aria Saboonchi Lilabady, Leo-Paul Morel] Goodbye, I am Justin Jin, a student of CSE330 Spring 2023\n");

}

module_init(Justin_Jin_Init);
module_exit(Justin_Jin_Exit);
MODULE_LICENSE("GPL");

