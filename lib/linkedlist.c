#include "linkedlist.h"

void* lst_delete(lst_node *lst_pointer, lst_node *toDelete)
{
    void *data;
    lst_node *temp;

    while(lst_pointer->next!=NULL && lst_pointer->next != toDelete)
        lst_pointer = lst_pointer -> next;

    if(lst_pointer->next==NULL) {
        printf("Element is not present in the list\n");
        return NULL;
    }
    temp = lst_pointer -> next;
    lst_pointer->next = temp->next;
    temp->prev =  lst_pointer;
    data = temp->data;
    free(temp);
    return data;
}

void lst_destroy(lst_node *lst_pointer) {
    lst_node *temp;
    while(lst_pointer->next !=NULL) {
        temp = lst_pointer->next;
        free(lst_pointer);
        lst_destroy(temp);
    }
}

void lst_insert(lst_node *lst_pointer, void *data)
{
    while(lst_pointer->next!=NULL)
    {
        lst_pointer = lst_pointer -> next;
    }
    lst_pointer->next = (lst_node *)malloc(sizeof(lst_node));
    (lst_pointer->next)->prev = lst_pointer;
    lst_pointer = lst_pointer->next;
    lst_pointer->data = data;
    lst_pointer->next = NULL;
}

void lst_insertAt(lst_node *lst_pointer, void *data, int index)
{
    int i=0;
    lst_node *temp;
    temp = lst_pointer;
    while(lst_pointer->next!=NULL && i<index)
        lst_pointer = lst_pointer->next;

    if(i!=index) {
        printf("No such index available..!!\n");
        return;
    }
    if(lst_pointer->next == NULL) {
        lst_insert(temp, data);
        return;
    }
    lst_insertAfter(temp, data, lst_pointer);
}

void lst_insertAfter(lst_node *lst_pointer, void *data, lst_node *afterThis)
{
    lst_node *temp;
    while(lst_pointer->next!=NULL && lst_pointer->next != afterThis)
        lst_pointer = lst_pointer->next;

    temp = lst_pointer->next;
    lst_pointer->next = (lst_node *)malloc(sizeof(lst_node));
    (lst_pointer->next)->prev = lst_pointer;
    lst_pointer = lst_pointer->next;
    lst_pointer->data = data;
    lst_pointer->next = temp;
    temp->prev = lst_pointer;
}

lst_node* lst_initiate(void *data) {
    lst_node *lst_pointer;
    lst_pointer = (lst_node *)malloc(sizeof(lst_node));
    lst_pointer->next = NULL;
    lst_pointer->prev = NULL;
    lst_pointer->data = data;
    return lst_pointer;
}
