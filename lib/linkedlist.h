#include <stdio.h>
#include <stdlib.h>

typedef struct lst_Node
{
    struct lst_Node *next;
    struct lst_Node *prev;
    void *data;
} lst_node;

void     *lst_delete(lst_node *lst_pointer, lst_node *toDelete);
void      lst_destroy(lst_node *lst_pointer);
void      lst_insert(lst_node *lst_pointer, void *data);
void      lst_insertAt(lst_node *lst_pointer, void *data, int index);
void      lst_insertAfter(lst_node *lst_pointer, void *data, lst_node *afterThis);
lst_node* lst_initiate(void *data);
