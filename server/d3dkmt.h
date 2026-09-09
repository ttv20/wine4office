/* Internal D3DKMT server helpers. */

#ifndef __WINE_SERVER_D3DKMT_H
#define __WINE_SERVER_D3DKMT_H

struct object;
struct process;

extern struct object *get_d3dkmt_object_handle( struct process *process,
                                                obj_handle_t handle,
                                                enum d3dkmt_type type );

#endif /* __WINE_SERVER_D3DKMT_H */
