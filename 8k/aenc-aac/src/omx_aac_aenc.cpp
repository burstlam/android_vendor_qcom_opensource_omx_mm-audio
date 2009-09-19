/*--------------------------------------------------------------------------
Copyright (c) 2009, Code Aurora Forum. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Code Aurora nor
      the names of its contributors may be used to endorse or promote
      products derived from this software without specific prior written
      permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------------*/
/*============================================================================
@file omx_aenc_aac.c
  This module contains the implementation of the OpenMAX core & component.

*//*========================================================================*/
//////////////////////////////////////////////////////////////////////////////
//                             Include Files
//////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include <fcntl.h>
#include <omx_aac_aenc.h>
#include <sys/ioctl.h>
#include <errno.h>

using namespace std;

void omx_aac_aenc::wait_for_event()
{
    pthread_mutex_lock(&m_event_lock);
    while(m_is_event_done == 0)
    {
        pthread_cond_wait(&cond, &m_event_lock);
    }
    m_is_event_done = 0;
    pthread_mutex_unlock(&m_event_lock);
}

void omx_aac_aenc::event_complete()
{
    pthread_mutex_lock(&m_event_lock);
    if(m_is_event_done == 0) {
        m_is_event_done = 1;
        pthread_cond_signal(&cond);
    }
    pthread_mutex_unlock(&m_event_lock);
}

// omx_cmd_queue destructor
omx_aac_aenc::omx_cmd_queue::~omx_cmd_queue()
{
    // Nothing to do
}

// omx cmd queue constructor
omx_aac_aenc::omx_cmd_queue::omx_cmd_queue(): m_read(0),m_write(0),m_size(0)
{
    memset(m_q,0,sizeof(omx_event)*OMX_CORE_CONTROL_CMDQ_SIZE);
}

// omx cmd queue insert
bool omx_aac_aenc::omx_cmd_queue::insert_entry(unsigned p1,
                                                unsigned p2,
                                                unsigned id)
{
    bool ret = true;
    if (m_size < OMX_CORE_CONTROL_CMDQ_SIZE)
    {
        m_q[m_write].id       = id;
        m_q[m_write].param1   = p1;
        m_q[m_write].param2   = p2;
        m_write++;
        m_size ++;
        if (m_write >= OMX_CORE_CONTROL_CMDQ_SIZE)
        {
            m_write = 0;
        }
    }
    else
    {
        ret = false;
        DEBUG_PRINT_ERROR("ERROR!!! Command Queue Full");
    }
    return ret;
}

bool omx_aac_aenc::omx_cmd_queue::pop_entry(unsigned *p1,
                                             unsigned *p2, unsigned *id)
{
    bool ret = true;
    if (m_size > 0)
    {
        *id = m_q[m_read].id;
        *p1 = m_q[m_read].param1;
        *p2 = m_q[m_read].param2;
        // Move the read pointer ahead
        ++m_read;
        --m_size;
        if (m_read >= OMX_CORE_CONTROL_CMDQ_SIZE)
        {
            m_read = 0;

        }
    }
    else
    {
        ret = false;
        DEBUG_PRINT_ERROR("ERROR Delete!!! Command Queue Empty");
    }
    return ret;
}

// factory function executed by the core to create instances
void *get_omx_component_factory_fn(void)
{
    return(new omx_aac_aenc);
}

void omx_aac_aenc::in_th_goto_sleep()
{
    pthread_mutex_lock(&m_in_th_lock);
    while (0 == m_is_in_th_sleep)
    {
        pthread_cond_wait(&in_cond, &m_in_th_lock);
    }
    m_is_in_th_sleep = 0;
    pthread_mutex_unlock(&m_in_th_lock);
}

void omx_aac_aenc::in_th_wakeup()
{
    pthread_mutex_lock(&m_in_th_lock);
    if (0 == m_is_in_th_sleep)
    {
        m_is_in_th_sleep = 1;
        pthread_cond_signal(&in_cond);
    }
    pthread_mutex_unlock(&m_in_th_lock);
}

void omx_aac_aenc::out_th_goto_sleep()
{

    pthread_mutex_lock(&m_out_th_lock);
    while (0 == m_is_out_th_sleep)
    {
        pthread_cond_wait(&out_cond, &m_out_th_lock);
    }
    m_is_out_th_sleep = 0;
    pthread_mutex_unlock(&m_out_th_lock);
}

void omx_aac_aenc::out_th_wakeup()
{
    pthread_mutex_lock(&m_out_th_lock);
    if (0 == m_is_out_th_sleep)
    {
        m_is_out_th_sleep = 1;
        pthread_cond_signal(&out_cond);
    }
    pthread_mutex_unlock(&m_out_th_lock);
}
/* ======================================================================
FUNCTION
  omx_aac_aenc::omx_aac_aenc

DESCRIPTION
  Constructor

PARAMETERS
  None

RETURN VALUE
  None.
========================================================================== */
omx_aac_aenc::omx_aac_aenc(): m_flush_cnt(255),
                                m_state(OMX_StateInvalid),
                                m_app_data(NULL),
                                m_inp_act_buf_count(OMX_CORE_NUM_INPUT_BUFFERS),
                                m_out_act_buf_count(OMX_CORE_NUM_OUTPUT_BUFFERS),
                                m_inp_current_buf_count(0),
                                m_out_current_buf_count(0),
                                m_inp_bEnabled(OMX_TRUE),
                                m_out_bEnabled(OMX_TRUE),
                                m_inp_bPopulated(OMX_FALSE),
                                m_out_bPopulated(OMX_FALSE),
                                m_ipc_to_in_th(NULL),
                                m_ipc_to_out_th(NULL),
                                m_ipc_to_cmd_th(NULL),
                                m_drv_fd(-1),
                                m_flags(0),
                                m_is_event_done(0),
                                is_in_th_sleep(false),
                                is_out_th_sleep(false),
                                m_idle_transition(0),
                                nTimestamp(0),
                                m_is_alloc_buf(0)
{
    memset(&m_cmp, 0, sizeof(m_cmp));
    memset(&m_cb, 0, sizeof(m_cb));

    pthread_mutexattr_init(&m_lock_attr);
    pthread_mutex_init(&m_lock, &m_lock_attr);

    pthread_mutexattr_init(&m_commandlock_attr);
    pthread_mutex_init(&m_commandlock, &m_commandlock_attr);

    pthread_mutexattr_init(&m_outputlock_attr);
    pthread_mutex_init(&m_outputlock, &m_outputlock_attr);

    pthread_mutexattr_init(&m_state_attr);
    pthread_mutex_init(&m_state_lock, &m_state_attr);

    pthread_mutexattr_init(&m_event_attr);
    pthread_mutex_init(&m_event_lock, &m_event_attr);

    pthread_mutexattr_init(&m_flush_attr);
    pthread_mutex_init(&m_flush_lock, &m_flush_attr);

    pthread_mutexattr_init(&m_in_th_attr);
    pthread_mutex_init(&m_in_th_lock, &m_in_th_attr);

    pthread_mutexattr_init(&m_out_th_attr);
    pthread_mutex_init(&m_out_th_lock, &m_out_th_attr);

    pthread_mutexattr_init(&m_in_th_attr_1);
    pthread_mutex_init(&m_in_th_lock_1, &m_in_th_attr_1);

    pthread_mutexattr_init(&m_out_th_attr_1);
    pthread_mutex_init(&m_out_th_lock_1, &m_out_th_attr_1);

    pthread_mutexattr_init(&out_buf_count_lock_attr);
    pthread_mutex_init(&out_buf_count_lock, &out_buf_count_lock_attr);

    pthread_mutexattr_init(&in_buf_count_lock_attr);
    pthread_mutex_init(&in_buf_count_lock, &in_buf_count_lock_attr);

    pthread_cond_init(&cond, 0);
    pthread_cond_init(&in_cond, 0);
    pthread_cond_init(&out_cond, 0);

    sem_init(&sem_States,0, 0);
    sem_init(&sem_read_msg,0, 0);
    sem_init(&sem_write_msg,0, 0);

    return;
}


/* ======================================================================
FUNCTION
  omx_aac_aenc::~omx_aac_aenc

DESCRIPTION
  Destructor

PARAMETERS
  None

RETURN VALUE
  None.
========================================================================== */
omx_aac_aenc::~omx_aac_aenc()
{
    DEBUG_PRINT("INSIDE DESTRUCTOR comp-deinit=%d\n",m_comp_deinit);
    if (m_comp_deinit == 0)
    {
        nNumInputBuf = 0;
        nNumOutputBuf = 0;
        m_is_alloc_buf = 0;
        m_out_act_buf_count = 0;
        m_inp_act_buf_count = 0;

        pthread_mutex_lock(&m_in_th_lock_1);
        if (is_in_th_sleep)
        {
            is_in_th_sleep = false;
            DEBUG_DETAIL("Deinit:WAKING UP IN THREADS\n");
            in_th_wakeup();
        }
        pthread_mutex_unlock(&m_in_th_lock_1);

        pthread_mutex_lock(&m_out_th_lock_1);
        if (is_out_th_sleep)
        {
            is_out_th_sleep = false;
            DEBUG_DETAIL("SCP:WAKING UP OUT THREADS\n");
            out_th_wakeup();

        }
        pthread_mutex_unlock(&m_out_th_lock_1);
        if (pcm_input)
        {
            if (m_ipc_to_in_th != NULL)
            {
                omx_aac_thread_stop(m_ipc_to_in_th);
                m_ipc_to_in_th = NULL;
            }
        }
        if (m_ipc_to_cmd_th != NULL)
        {
            omx_aac_thread_stop(m_ipc_to_cmd_th);
            m_ipc_to_cmd_th = NULL;
        }
        {
            if (m_ipc_to_out_th != NULL)
            {
                omx_aac_thread_stop(m_ipc_to_out_th);
                m_ipc_to_out_th = NULL;
            }
        }
        m_idle_transition = 0;
        m_inp_current_buf_count=0;
        m_out_current_buf_count=0;
        m_inp_bEnabled = OMX_TRUE;
        m_out_bEnabled = OMX_TRUE;
        m_inp_bPopulated = OMX_FALSE;
        m_out_bPopulated = OMX_FALSE;
        if (m_drv_fd >= 0)
        {
            close(m_drv_fd);
        } else
        {
            DEBUG_PRINT_ERROR(" AAC device already closed \n");
        }
    }
    pthread_mutexattr_destroy(&m_lock_attr);
    pthread_mutex_destroy(&m_lock);

    pthread_mutexattr_destroy(&m_commandlock_attr);
    pthread_mutex_destroy(&m_commandlock);

    pthread_mutexattr_destroy(&m_outputlock_attr);
    pthread_mutex_destroy(&m_outputlock);

    pthread_mutexattr_destroy(&m_state_attr);
    pthread_mutex_destroy(&m_state_lock);

    pthread_mutexattr_destroy(&m_event_attr);
    pthread_mutex_destroy(&m_event_lock);

    pthread_mutexattr_destroy(&m_flush_attr);
    pthread_mutex_destroy(&m_flush_lock);

    pthread_mutexattr_destroy(&m_in_th_attr);
    pthread_mutex_destroy(&m_in_th_lock);

    pthread_mutexattr_destroy(&m_out_th_attr);
    pthread_mutex_destroy(&m_out_th_lock);

    pthread_mutexattr_destroy(&m_in_th_attr_1);
    pthread_mutex_destroy(&m_in_th_lock_1);

    pthread_mutexattr_destroy(&m_out_th_attr_1);
    pthread_mutex_destroy(&m_out_th_lock_1);

    pthread_mutex_destroy(&out_buf_count_lock);
    pthread_mutex_destroy(&in_buf_count_lock);
    pthread_cond_destroy(&cond);
    pthread_cond_destroy(&in_cond);
    pthread_cond_destroy(&out_cond);
    sem_destroy (&sem_read_msg);
    sem_destroy (&sem_write_msg);
    sem_destroy (&sem_States);
    return;
}

/**
  @brief memory function for sending EmptyBufferDone event
   back to IL client

  @param bufHdr OMX buffer header to be passed back to IL client
  @return none
 */
void omx_aac_aenc::buffer_done_cb(OMX_BUFFERHEADERTYPE *bufHdr)
{
    if (m_cb.EmptyBufferDone)
    {
        PrintFrameHdr(OMX_COMPONENT_GENERATE_BUFFER_DONE,bufHdr);
        bufHdr->nFilledLen = 0;
        m_cb.EmptyBufferDone(&m_cmp, m_app_data, bufHdr);
        pthread_mutex_lock(&in_buf_count_lock);
        nNumInputBuf--;
        pthread_mutex_unlock(&in_buf_count_lock);
    }

    return;
}

/*=============================================================================
FUNCTION:
  flush_ack

DESCRIPTION:


INPUT/OUTPUT PARAMETERS:
  None

RETURN VALUE:
  None

Dependency:
  None

SIDE EFFECTS:
  None
=============================================================================*/
void omx_aac_aenc::flush_ack()
{
    // Decrement the FLUSH ACK count and notify the waiting recepients
    pthread_mutex_lock(&m_flush_lock);
    --m_flush_cnt;
    if (0 == m_flush_cnt)
    {
        event_complete();
    }
    DEBUG_PRINT("Rxed FLUSH ACK cnt=%d\n",m_flush_cnt);
    pthread_mutex_unlock(&m_flush_lock);
}
void omx_aac_aenc::frame_done_cb(OMX_BUFFERHEADERTYPE *bufHdr)
{
    if (m_cb.FillBufferDone)
    {
        if (fcount == 0) {
            bufHdr->nTimeStamp = nTimestamp;
        }
        else
        {
            nTimestamp += frameDuration;
            bufHdr->nTimeStamp = nTimestamp;
        }
        m_cb.FillBufferDone(&m_cmp, m_app_data, bufHdr);
        pthread_mutex_lock(&out_buf_count_lock);
        nNumOutputBuf--;
        pthread_mutex_unlock(&out_buf_count_lock);
        PrintFrameHdr(OMX_COMPONENT_GENERATE_FRAME_DONE,bufHdr);
        fcount++;
    }

    return;
}

/*=============================================================================
FUNCTION:
  process_out_port_msg

DESCRIPTION:
  Function for handling all commands from IL client
IL client commands are processed and callbacks are generated through
this routine  Audio Command Server provides the thread context for this routine

INPUT/OUTPUT PARAMETERS:
  [INOUT] client_data
  [IN] id

RETURN VALUE:
  None

Dependency:
  None

SIDE EFFECTS:
  None
=============================================================================*/
void omx_aac_aenc::process_out_port_msg(void *client_data, unsigned char id)
{
    unsigned      p1;                            // Parameter - 1
    unsigned      p2;                            // Parameter - 2
    unsigned      ident;
    unsigned      qsize     = 0;                 // qsize
    unsigned      tot_qsize = 0;
    omx_aac_aenc  *pThis    = (omx_aac_aenc *) client_data;
    OMX_STATETYPE state;
    pthread_mutex_lock(&pThis->m_state_lock);
    pThis->get_state(&pThis->m_cmp, &state);
    pthread_mutex_unlock(&pThis->m_state_lock);
    if (state == OMX_StateLoaded)
    {
        DEBUG_PRINT(" OUT: IN LOADED STATE RETURN\n");
        return;
    }
    pthread_mutex_lock(&pThis->m_outputlock);
    qsize = pThis->m_output_ctrl_cmd_q.m_size;

    if ((state != OMX_StateExecuting) && !qsize)
    {
        pthread_mutex_unlock(&pThis->m_outputlock);
        pthread_mutex_lock(&pThis->m_state_lock);
        pThis->get_state(&pThis->m_cmp, &state);
        pthread_mutex_unlock(&pThis->m_state_lock);
        if (state == OMX_StateLoaded)
            return;

        DEBUG_DETAIL("OUT:1.SLEEPING OUT THREAD\n");
        pthread_mutex_lock(&pThis->m_out_th_lock_1);
        pThis->is_out_th_sleep = true;
        pthread_mutex_unlock(&pThis->m_out_th_lock_1);
        pThis->out_th_goto_sleep();

        /* Get the updated state */
        pthread_mutex_lock(&pThis->m_state_lock);
        pThis->get_state(&pThis->m_cmp, &state);
        pthread_mutex_unlock(&pThis->m_state_lock);
    }

    if (((!pThis->m_output_ctrl_cmd_q.m_size) && !pThis->m_out_bEnabled))
    {
        // case where no port reconfig and nothing in the flush q
        DEBUG_DETAIL("No flush/port reconfig qsize=%d tot_qsize=%d",\
            qsize,tot_qsize);
        pthread_mutex_unlock(&pThis->m_outputlock);
        pthread_mutex_lock(&pThis->m_state_lock);
        pThis->get_state(&pThis->m_cmp, &state);
        pthread_mutex_unlock(&pThis->m_state_lock);
        if (state == OMX_StateLoaded)
            return;

        DEBUG_PRINT("OUT:2. SLEEPING OUT THREAD \n");
        pthread_mutex_lock(&pThis->m_out_th_lock_1);
        pThis->is_out_th_sleep = true;
        pthread_mutex_unlock(&pThis->m_out_th_lock_1);
        pThis->out_th_goto_sleep();

        //return;
    }
    tot_qsize = pThis->m_output_ctrl_cmd_q.m_size;
    tot_qsize += pThis->m_output_ctrl_fbd_q.m_size;
    tot_qsize += pThis->m_output_q.m_size;


    DEBUG_DETAIL("OUT-->QSIZE-flush=%d,fbd=%d QSIZE=%d state=%d\n",\
        pThis->m_output_ctrl_cmd_q.m_size,
        pThis->m_output_ctrl_fbd_q.m_size,
        pThis->m_output_q.m_size,state);

    if (0 ==tot_qsize)
    {
        pthread_mutex_unlock(&pThis->m_outputlock);
        DEBUG_DETAIL("OUT-->BREAK FROM LOOP...%d\n",tot_qsize);
        return;
    }
    if (qsize)
    {
        // process FLUSH message
        pThis->m_output_ctrl_cmd_q.pop_entry(&p1,&p2,&ident);
    } else if ((qsize = pThis->m_output_ctrl_fbd_q.m_size) &&
        (pThis->m_out_bEnabled) && (state == OMX_StateExecuting))
    {
        // then process EBD's
        pThis->m_output_ctrl_fbd_q.pop_entry(&p1,&p2,&ident);
    } else if ((qsize = pThis->m_output_q.m_size) &&
        (pThis->m_out_bEnabled) && (state == OMX_StateExecuting))
    {
        // if no FLUSH and FBD's then process FTB's
        pThis->m_output_q.pop_entry(&p1,&p2,&ident);
    } else if (state == OMX_StateLoaded)
    {
        pthread_mutex_unlock(&pThis->m_outputlock);
        DEBUG_PRINT("IN: ***in OMX_StateLoaded so exiting\n");
        return ;
    } else
    {
        qsize = 0;
    }
    pthread_mutex_unlock(&pThis->m_outputlock);

    if (qsize > 0)
    {
        id = ident;
        DEBUG_DETAIL("OUT->state[%d]ident[%d]flushq[%d]fbd[%d]dataq[%d]\n",\
            pThis->m_state,
            ident,
            pThis->m_output_ctrl_cmd_q.m_size,
            pThis->m_output_ctrl_fbd_q.m_size,
            pThis->m_output_q.m_size);

        if (OMX_COMPONENT_GENERATE_FRAME_DONE == id)
        {
            pThis->frame_done_cb((OMX_BUFFERHEADERTYPE *)p2);
        } else if (OMX_COMPONENT_GENERATE_FTB == id)
        {
            pThis->fill_this_buffer_proxy((OMX_HANDLETYPE)p1,
                (OMX_BUFFERHEADERTYPE *)p2);
        } else if (OMX_COMPONENT_GENERATE_EOS == id)
        {
            pThis->m_cb.EventHandler(&pThis->m_cmp,
                pThis->m_app_data,
                OMX_EventBufferFlag,
                1, 1, NULL );
        } else if (OMX_COMPONENT_GENERATE_COMMAND == id)
        {
            // Execute FLUSH command
            if (OMX_CommandFlush == p1)
            {
                DEBUG_DETAIL("Executing FLUSH command on Output port\n");
                pThis->execute_output_omx_flush();
            } else
            {
                DEBUG_DETAIL("Invalid command[%d]\n",p1);
            }
        } else
        {
            DEBUG_PRINT_ERROR("ERROR:OUT-->Invalid Id[%d]\n",id);
        }
    } else
    {
        DEBUG_DETAIL("ERROR: OUT--> Empty OUTPUTQ\n");
    }

    return;
}

/*=============================================================================
FUNCTION:
  process_command_msg

DESCRIPTION:


INPUT/OUTPUT PARAMETERS:
  [INOUT] client_data
  [IN] id

RETURN VALUE:
  None

Dependency:
  None

SIDE EFFECTS:
  None
=============================================================================*/
void omx_aac_aenc::process_command_msg(void *client_data, unsigned char id)
{
    unsigned     p1;                             // Parameter - 1
    unsigned     p2;                             // Parameter - 2
    unsigned     ident;
    unsigned     qsize  = 0;
    omx_aac_aenc *pThis = (omx_aac_aenc*)client_data;
    pthread_mutex_lock(&pThis->m_commandlock);

    qsize = pThis->m_command_q.m_size;
    DEBUG_DETAIL("CMD-->QSIZE=%d state=%d\n",pThis->m_command_q.m_size,
        pThis->m_state);

    if (!qsize)
    {
        DEBUG_DETAIL("CMD-->BREAKING FROM LOOP\n");
        pthread_mutex_unlock(&pThis->m_commandlock);
        return;
    } else
    {
        pThis->m_command_q.pop_entry(&p1,&p2,&ident);
    }
    pthread_mutex_unlock(&pThis->m_commandlock);

    id = ident;
    DEBUG_DETAIL("CMD->state[%d]id[%d]cmdq[%d]n",\
        pThis->m_state,ident, \
        pThis->m_command_q.m_size);

    if (OMX_COMPONENT_GENERATE_EVENT == id)
    {
        if (pThis->m_cb.EventHandler)
        {
            if (OMX_CommandStateSet == p1)
            {
                pthread_mutex_lock(&pThis->m_state_lock);
                pThis->m_state = (OMX_STATETYPE) p2;
                pthread_mutex_unlock(&pThis->m_state_lock);
                DEBUG_PRINT("CMD:Process->state set to %d \n", \
                    pThis->m_state);

                if (pThis->m_state == OMX_StateExecuting ||
                    pThis->m_state == OMX_StateLoaded)
                {

                    pthread_mutex_lock(&pThis->m_in_th_lock_1);
                    if (pThis->is_in_th_sleep)
                    {
                        pThis->is_in_th_sleep = false;
                        DEBUG_DETAIL("CMD:WAKING UP IN THREADS\n");
                        pThis->in_th_wakeup();
                    }
                    pthread_mutex_unlock(&pThis->m_in_th_lock_1);

                    pthread_mutex_lock(&pThis->m_out_th_lock_1);
                    if (pThis->is_out_th_sleep)
                    {
                        DEBUG_DETAIL("CMD:WAKING UP OUT THREADS\n");
                        pThis->is_out_th_sleep = false;
                        pThis->out_th_wakeup();
                    }
                    pthread_mutex_unlock(&pThis->m_out_th_lock_1);
                }
            }
            if (OMX_StateInvalid == pThis->m_state)
            {
                pThis->m_cb.EventHandler(&pThis->m_cmp,
                    pThis->m_app_data,
                    OMX_EventError,
                    OMX_ErrorInvalidState,
                    0, NULL );
            } else if (p2 == OMX_ErrorPortUnpopulated)
            {
                pThis->m_cb.EventHandler(&pThis->m_cmp,
                    pThis->m_app_data,
                    OMX_EventError,
                    p2,
                    NULL,
                    NULL );
            } else
            {
                pThis->m_cb.EventHandler(&pThis->m_cmp,
                    pThis->m_app_data,
                    OMX_EventCmdComplete,
                    p1, p2, NULL );
            }
        } else
        {
            DEBUG_PRINT_ERROR("ERROR:CMD-->EventHandler NULL \n");
        }
    } else if (OMX_COMPONENT_GENERATE_COMMAND == id)
    {
        pThis->send_command_proxy(&pThis->m_cmp,
            (OMX_COMMANDTYPE)p1,
            (OMX_U32)p2,(OMX_PTR)NULL);
    } else if (OMX_COMPONENT_PORTSETTINGS_CHANGED == id)
    {
        DEBUG_DETAIL("CMD-->RXED PORTSETTINGS_CHANGED");
        pThis->m_cb.EventHandler(&pThis->m_cmp,
            pThis->m_app_data,
            OMX_EventPortSettingsChanged,
            1, 1, NULL );
    } else if (pThis->m_state != OMX_StateExecuting)
    {
        DEBUG_PRINT("CMD: ***not in executing state so exiting\n");
        return ;
    } else
    {
        DEBUG_PRINT_ERROR("ERROR:CMD-->incorrect event[%d]\n",id);
    }
    return;
}

/*=============================================================================
FUNCTION:
  process_in_port_msg

DESCRIPTION:


INPUT/OUTPUT PARAMETERS:
  [INOUT] client_data
  [IN] id

RETURN VALUE:
  None

Dependency:
  None

SIDE EFFECTS:
  None
=============================================================================*/
void omx_aac_aenc::process_in_port_msg(void *client_data, unsigned char id)
{
    unsigned      p1;                            // Parameter - 1
    unsigned      p2;                            // Parameter - 2
    unsigned      ident;
    unsigned      qsize     = 0;
    unsigned      tot_qsize = 0;
    omx_aac_aenc  *pThis    = (omx_aac_aenc *) client_data;
    OMX_STATETYPE state;

    if (!pThis)
    {
        DEBUG_PRINT_ERROR("ERROR:IN--> Invalid Obj \n");
        return;
    }

    pthread_mutex_lock(&pThis->m_state_lock);
    pThis->get_state(&pThis->m_cmp, &state);
    pthread_mutex_unlock(&pThis->m_state_lock);

    if (state == OMX_StateLoaded)
    {
        DEBUG_PRINT(" IN: IN LOADED STATE RETURN\n");
        return;
    }
    // Protect the shared queue data structure
    pthread_mutex_lock(&pThis->m_lock);
    qsize = pThis->m_input_ctrl_cmd_q.m_size;
    if (state != OMX_StateExecuting && !qsize)
    {
        pthread_mutex_unlock(&pThis->m_lock);
        if (state == OMX_StateLoaded)
            return;

        DEBUG_DETAIL("SLEEPING IN THREAD\n");
        pthread_mutex_lock(&pThis->m_in_th_lock_1);
        pThis->is_in_th_sleep = true;
        pthread_mutex_unlock(&pThis->m_in_th_lock_1);
        pThis->in_th_goto_sleep();

        /* Get the updated state */
        pthread_mutex_lock(&pThis->m_state_lock);
        pThis->get_state(&pThis->m_cmp, &state);
        pthread_mutex_unlock(&pThis->m_state_lock);
    }
    tot_qsize = qsize;
    tot_qsize += pThis->m_input_ctrl_ebd_q.m_size;
    tot_qsize += pThis->m_input_q.m_size;

    DEBUG_DETAIL("Input-->QSIZE-flush=%d,ebd=%d QSIZE=%d state=%d\n",\
        pThis->m_input_ctrl_cmd_q.m_size,
        pThis->m_input_ctrl_ebd_q.m_size,
        pThis->m_input_q.m_size, state);


    if (0 == tot_qsize)
    {
        DEBUG_DETAIL("IN-->BREAKING FROM IN LOOP");
        pthread_mutex_unlock(&pThis->m_lock);
        return;
    }

    if (qsize)
    {
        // process FLUSH message
        pThis->m_input_ctrl_cmd_q.pop_entry(&p1,&p2,&ident);
    } else if ((qsize = pThis->m_input_ctrl_ebd_q.m_size) &&
        (state == OMX_StateExecuting))
    {
        // then process EBD's
        pThis->m_input_ctrl_ebd_q.pop_entry(&p1,&p2,&ident);
    } else if ((qsize = pThis->m_input_q.m_size) &&
        (state == OMX_StateExecuting))
    {
        // if no FLUSH and EBD's then process ETB's
        pThis->m_input_q.pop_entry(&p1, &p2, &ident);
    } else if (state == OMX_StateLoaded)
    {
        pthread_mutex_unlock(&pThis->m_lock);
        DEBUG_PRINT("IN: ***in OMX_StateLoaded so exiting\n");
        return ;
    } else
    {
        qsize = 0;
    }
    pthread_mutex_unlock(&pThis->m_lock);

    if (qsize > 0)
    {
        id = ident;
        DEBUG_DETAIL("Input->state[%d]id[%d]flushq[%d]ebdq[%d]dataq[%d]\n",\
            pThis->m_state,
            ident,
            pThis->m_input_ctrl_cmd_q.m_size,
            pThis->m_input_ctrl_ebd_q.m_size,
            pThis->m_input_q.m_size);
        if (OMX_COMPONENT_GENERATE_BUFFER_DONE == id)
        {
            pThis->buffer_done_cb((OMX_BUFFERHEADERTYPE *)p2);
        }
        else if(id == OMX_COMPONENT_GENERATE_EOS)
        {
            pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                OMX_EventBufferFlag, 0, 1, NULL );
        } else if (OMX_COMPONENT_GENERATE_ETB == id)
        {
            pThis->empty_this_buffer_proxy((OMX_HANDLETYPE)p1,
                (OMX_BUFFERHEADERTYPE *)p2);
        } else if (OMX_COMPONENT_GENERATE_COMMAND == id)
        {
            // Execute FLUSH command
            if (OMX_CommandFlush == p1)
            {
                DEBUG_DETAIL(" Executing FLUSH command on Input port\n");
                pThis->execute_input_omx_flush();
            } else
            {
                DEBUG_DETAIL("Invalid command[%d]\n",p1);
            }
        } else
        {
            DEBUG_PRINT_ERROR("ERROR:IN-->Invalid Id[%d]\n",id);
        }
    } else
    {
        DEBUG_DETAIL("ERROR:IN-->Empty INPUT Q\n");
    }
    return;
}

/**
 @brief member function for performing component initialization

 @param role C string mandating role of this component
 @return Error status
 */
OMX_ERRORTYPE omx_aac_aenc::component_init(OMX_STRING role)
{

    OMX_ERRORTYPE eRet = OMX_ErrorNone;

    /* Ignore role */

    m_state                   = OMX_StateLoaded;
    /* DSP does not give information about the bitstream
    randomly assign the value right now. Query will result in
    incorrect param */
    memset(&m_aac_param, 0, sizeof(m_aac_param));
    m_aac_param.nSize = sizeof(m_aac_param);
    m_aac_param.nSampleRate = OMX_AAC_DEFAULT_SF;
    m_aac_param.nChannels = OMX_AAC_DEFAULT_CH_CFG;
    m_volume = OMX_AAC_DEFAULT_VOL;             /* Close to unity gain */
    /* default calculation of frame duration */
    frameDuration = ((OMX_AAC_OUTPUT_BUFFER_SIZE*1000) /
        (OMX_AAC_DEFAULT_SF * 2 *OMX_AAC_DEFAULT_CH_CFG));
    fcount = 0;
    nTimestamp = 0;

    pcm_input = 0;/* by default enable tunnel mode */
    nNumInputBuf = 0;
    nNumOutputBuf = 0;
    m_ipc_to_in_th = NULL;                       // Command server instance
    m_ipc_to_out_th = NULL;                      // Client server instance
    m_ipc_to_cmd_th = NULL;                      // command instance
    m_is_out_th_sleep = 0;
    m_is_in_th_sleep = 0;
    is_out_th_sleep= false;
    is_in_th_sleep=false;
    m_idle_transition = 0;

    m_comp_deinit=0;
    memset(&m_priority_mgm, 0, sizeof(m_priority_mgm));
    m_priority_mgm.nGroupID =0;
    m_priority_mgm.nGroupPriority=0;

    memset(&m_buffer_supplier, 0, sizeof(m_buffer_supplier));
    m_buffer_supplier.nPortIndex=OMX_BufferSupplyUnspecified;
    DEBUG_PRINT(" component init: role = %s\n",role);
    if (!strcmp(role,"OMX.qcom.audio.encoder.aac"))
    {
        pcm_input = 1;
        DEBUG_PRINT("\ncomponent_init: Component %s LOADED \n", role);
    } else if (!strcmp(role,"OMX.qcom.audio.encoder.tunneled.aac"))
    {
        pcm_input = 0;
        DEBUG_PRINT("\ncomponent_init: Component %s LOADED \n", role);
    } else
    {
        DEBUG_PRINT("\ncomponent_init: Component %s LOADED is invalid\n", role);
    }

    if (pcm_input)
    {
        if (!m_ipc_to_in_th)
        {
            m_ipc_to_in_th = omx_aac_thread_create(process_in_port_msg,
                this,"INPUT_THREAD");
            if (!m_ipc_to_in_th)
            {
                DEBUG_PRINT_ERROR("ERROR!!! Failed to start Input port thread\n");
                return OMX_ErrorInsufficientResources;
            }
        }
    }

    if (!m_ipc_to_cmd_th)
    {
        m_ipc_to_cmd_th = omx_aac_thread_create(process_command_msg,
            this,"CMD_THREAD");
        if (!m_ipc_to_cmd_th)
        {
            DEBUG_PRINT_ERROR("ERROR!!!Failed to start "
                "command message thread\n");
            return OMX_ErrorInsufficientResources;
        }
    }

    if (!m_ipc_to_out_th)
    {
        m_ipc_to_out_th = omx_aac_thread_create(process_out_port_msg,
            this,"OUTPUT_THREAD");
        if (!m_ipc_to_out_th)
        {
            DEBUG_PRINT_ERROR("ERROR!!! Failed to start output "
                "port thread\n");
            return OMX_ErrorInsufficientResources;
        }
    }

    return eRet;
}

/**

 @brief member function to retrieve version of component



 @param hComp handle to this component instance
 @param componentName name of component
 @param componentVersion  pointer to memory space which stores the
       version number
 @param specVersion pointer to memory sapce which stores version of
        openMax specification
 @param componentUUID
 @return Error status
 */
OMX_ERRORTYPE  omx_aac_aenc::get_component_version
(
    OMX_IN OMX_HANDLETYPE               hComp,
    OMX_OUT OMX_STRING          componentName,
    OMX_OUT OMX_VERSIONTYPE* componentVersion,
    OMX_OUT OMX_VERSIONTYPE*      specVersion,
    OMX_OUT OMX_UUIDTYPE*       componentUUID)
{
    if (m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Get Comp Version in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    /* TBD -- Return the proper version */
    return OMX_ErrorNone;
}
/**
  @brief member function handles command from IL client

  This function simply queue up commands from IL client.
  Commands will be processed in command server thread context later

  @param hComp handle to component instance
  @param cmd type of command
  @param param1 parameters associated with the command type
  @param cmdData
  @return Error status
*/
OMX_ERRORTYPE  omx_aac_aenc::send_command(OMX_IN OMX_HANDLETYPE hComp,
                                           OMX_IN OMX_COMMANDTYPE  cmd,
                                           OMX_IN OMX_U32       param1,
                                           OMX_IN OMX_PTR      cmdData)
{
    int portIndex = (int)param1;
    if (OMX_StateInvalid == m_state)
    {
        return OMX_ErrorInvalidState;
    }
    if ( (cmd == OMX_CommandFlush) && (portIndex > 1) )
    {
        return OMX_ErrorBadPortIndex;
    }

    post_command((unsigned)cmd,(unsigned)param1,OMX_COMPONENT_GENERATE_COMMAND);
    DEBUG_PRINT_ERROR("Send Command : returns with OMX_ErrorNone \n");
    DEBUG_PRINT("send_command : recieved state before semwait= %d\n",param1);
    sem_wait (&sem_States);
    DEBUG_PRINT("send_command : recieved state after semwait\n");
    return OMX_ErrorNone;
}

/**
 @brief member function performs actual processing of commands excluding
  empty buffer call

 @param hComp handle to component
 @param cmd command type
 @param param1 parameter associated with the command
 @param cmdData

 @return error status
*/
OMX_ERRORTYPE  omx_aac_aenc::send_command_proxy(OMX_IN OMX_HANDLETYPE hComp,
                                                 OMX_IN OMX_COMMANDTYPE  cmd,
                                                 OMX_IN OMX_U32       param1,
                                                 OMX_IN OMX_PTR      cmdData)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    //   Handle only IDLE and executing
    OMX_BUFFERHEADERTYPE* buffer;
    OMX_STATETYPE eState = (OMX_STATETYPE) param1;
    int bFlag = 1;
    if (OMX_CommandStateSet == cmd)
    {
        /***************************/
        /* Current State is Loaded */
        /***************************/
        if (OMX_StateLoaded == m_state)
        {
            if (OMX_StateIdle == eState)
            {
                m_drv_fd = open("/dev/msm_aac_in", O_RDWR);
                if (m_drv_fd < 0)
                {
                    DEBUG_PRINT_ERROR("SCP-->Dev Open Failed[%d] errno[%d]",\
                        m_drv_fd,errno);

                    eState = OMX_StateInvalid;
                } else
                {
                    DEBUG_PRINT("SCP:Loaded-->Idle fd[%d]\n",m_drv_fd);

                    if (allocate_done() ||
                        (m_inp_bEnabled == OMX_FALSE
                        && m_out_bEnabled == OMX_FALSE))
                    {
                        DEBUG_PRINT("SCP-->Allocate Done Complete\n");
                    } else
                    {
                        DEBUG_PRINT("SCP-->Loaded to Idle-Pending\n");
                        BITMASK_SET(&m_flags, OMX_COMPONENT_IDLE_PENDING);
                        bFlag = 0;
                    }
                }
            } else if (eState == OMX_StateLoaded)
            {
                DEBUG_PRINT("OMXCORE-SM: Loaded-->Loaded\n");
                m_cb.EventHandler(&this->m_cmp,
                    this->m_app_data,
                    OMX_EventError,
                    OMX_ErrorSameState,
                    0, NULL );
                eRet = OMX_ErrorSameState;
            }

            else if (eState == OMX_StateWaitForResources)
            {
                DEBUG_PRINT("OMXCORE-SM: Loaded-->WaitForResources\n");
                eRet = OMX_ErrorNone;
            }

            else if (eState == OMX_StateExecuting)
            {
                DEBUG_PRINT("OMXCORE-SM: Loaded-->Executing\n");
                m_cb.EventHandler(&this->m_cmp,
                    this->m_app_data,
                    OMX_EventError,
                    OMX_ErrorIncorrectStateTransition,
                    0, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            }

            else if (eState == OMX_StatePause)
            {
                DEBUG_PRINT("OMXCORE-SM: Loaded-->Pause\n");
                m_cb.EventHandler(&this->m_cmp,
                    this->m_app_data,
                    OMX_EventError,
                    OMX_ErrorIncorrectStateTransition,
                    0, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            }

            else if (eState == OMX_StateInvalid)
            {
                DEBUG_PRINT("OMXCORE-SM: Loaded-->Invalid\n");
                m_cb.EventHandler(&this->m_cmp,
                    this->m_app_data,
                    OMX_EventError,
                    OMX_ErrorInvalidState,
                    0, NULL );
                m_state = OMX_StateInvalid;
                eRet = OMX_ErrorInvalidState;
            } else
            {
                DEBUG_PRINT_ERROR("SCP-->Loaded to Invalid(%d))\n",eState);
                eRet = OMX_ErrorBadParameter;
            }
        }

        /***************************/
        /* Current State is IDLE */
        /***************************/
        else if (OMX_StateIdle == m_state)
        {
            if (OMX_StateLoaded == eState)
            {
                if (release_done(-1))
                {
                    if (ioctl(m_drv_fd, AUDIO_STOP, 0) == -1)
                    {
                        DEBUG_PRINT("SCP:Idle->Loaded,ioctl stop failed %d\n",\
                            errno);
                    }
                    if (m_drv_fd >= 0)
                        close(m_drv_fd);
                    m_drv_fd = -1;
                    nTimestamp=0;

                    DEBUG_PRINT("SCP-->Idle to Loaded\n");
                } else
                {
                    DEBUG_PRINT("SCP--> Idle to Loaded-Pending\n");
                    BITMASK_SET(&m_flags, OMX_COMPONENT_LOADING_PENDING);
                    // Skip the event notification
                    bFlag = 0;
                }
            } else if (OMX_StateExecuting == eState)
            {
                struct msm_audio_config drv_config;
                DEBUG_PRINT("configure Driver for AAC Encoding " \
                    "sample rate = %d \n",m_aac_param.nSampleRate);
                ioctl(m_drv_fd, AUDIO_GET_CONFIG, &drv_config);
                drv_config.sample_rate = m_aac_param.nSampleRate;
                drv_config.channel_count = m_aac_param.nChannels;
                drv_config.type = 1;   // AAC encoding
                ioctl(m_drv_fd, AUDIO_SET_CONFIG, &drv_config);
                ioctl(m_drv_fd, AUDIO_START, 0);
                DEBUG_PRINT("SCP-->Idle to Executing\n");
            } else if (eState == OMX_StateIdle)
            {
                DEBUG_PRINT("OMXCORE-SM: Idle-->Idle\n");
                m_cb.EventHandler(&this->m_cmp,
                    this->m_app_data,
                    OMX_EventError,
                    OMX_ErrorSameState,
                    0, NULL );
                eRet = OMX_ErrorSameState;
            } else if (eState == OMX_StateWaitForResources)
            {
                DEBUG_PRINT("OMXCORE-SM: Idle-->WaitForResources\n");
                this->m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                    OMX_EventError,
                    OMX_ErrorIncorrectStateTransition,
                    0, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            }

            else if (eState == OMX_StatePause)
            {
                DEBUG_PRINT("OMXCORE-SM: Idle-->Pause\n");
            }

            else if (eState == OMX_StateInvalid)
            {
                DEBUG_PRINT("OMXCORE-SM: Idle-->Invalid\n");
                m_state = OMX_StateInvalid;
                this->m_cb.EventHandler(&this->m_cmp,
                    this->m_app_data,
                    OMX_EventError,
                    OMX_ErrorInvalidState,
                    0, NULL );
                eRet = OMX_ErrorInvalidState;
            } else
            {
                DEBUG_PRINT_ERROR("SCP--> Idle to %d Not Handled\n",eState);
                eRet = OMX_ErrorBadParameter;
            }
        }

        /******************************/
        /* Current State is Executing */
        /******************************/
        else if (OMX_StateExecuting == m_state)
        {
            if (OMX_StateIdle == eState)
            {
                DEBUG_PRINT("SCP-->Executing to Idle \n");
                m_idle_transition = 1;
                if (0 == pcm_input)
                {
                    // execute_omx_flush(1,false);  // Flush all ports
                } else
                {
                    execute_omx_flush(-1,false); // Flush all ports
                }
                pthread_mutex_lock(&out_buf_count_lock);
                if(nNumOutputBuf)
                {
                    DEBUG_PRINT("posting sem_States\n");
                    sem_post (&sem_States);
                    pthread_mutex_unlock(&out_buf_count_lock);
                    wait_for_event();
                }
                else
                {
                    pthread_mutex_unlock(&out_buf_count_lock);
                }
                DEBUG_PRINT("SCP-->WAITDONE -->m_idle_transition=%d nNumOutputBuf=%d\n",m_idle_transition,nNumOutputBuf);

            } else if (OMX_StatePause == eState)
            {
                DEBUG_DETAIL("*************************\n");
                DEBUG_PRINT("SCP-->RXED PAUSE STATE\n");
                DEBUG_DETAIL("*************************\n");
                //ioctl(m_drv_fd, AUDIO_PAUSE, 0);
            } else if (eState == OMX_StateLoaded)
            {
                DEBUG_PRINT("\n OMXCORE-SM: Executing --> Loaded \n");
                this->m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                    OMX_EventError,
                    OMX_ErrorIncorrectStateTransition,
                    0, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            } else if (eState == OMX_StateWaitForResources)
            {
                DEBUG_PRINT("\n OMXCORE-SM: Executing --> WaitForResources \n");
                this->m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                    OMX_EventError,
                    OMX_ErrorIncorrectStateTransition,
                    0, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            } else if (eState == OMX_StateExecuting)
            {
                DEBUG_PRINT("\n OMXCORE-SM: Executing --> Executing \n");
                this->m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                    OMX_EventError, OMX_ErrorSameState,
                    0, NULL );
                eRet = OMX_ErrorSameState;
            } else if (eState == OMX_StateInvalid)
            {
                DEBUG_PRINT("\n OMXCORE-SM: Executing --> Invalid \n");
                m_state = OMX_StateInvalid;
                this->m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                    OMX_EventError, OMX_ErrorInvalidState,
                    0, NULL );
                eRet = OMX_ErrorInvalidState;
            } else
            {
                DEBUG_PRINT_ERROR("SCP--> Executing to %d Not Handled\n",eState);
                eRet = OMX_ErrorBadParameter;
            }
        }
        /***************************/
        /* Current State is Pause  */
        /***************************/
        else if (OMX_StatePause == m_state)
        {
            if (OMX_StateExecuting == eState)
            {
                /* ioctl(m_drv_fd, AUDIO_RESUME, 0);
                Not implemented at this point */
                DEBUG_PRINT("SCP-->Paused to Executing \n");
            } else if (OMX_StateIdle == eState)
            {
                DEBUG_PRINT("SCP-->Paused to Idle \n");
                pthread_mutex_lock(&m_flush_lock);
                m_flush_cnt = 2;
                pthread_mutex_unlock(&m_flush_lock);
                if (0 == pcm_input)
                {
                    execute_omx_flush(1,false);  // Flush all ports
                } else
                {
                    execute_omx_flush(-1,false); // Flush all ports
                }
            } else if (eState == OMX_StateLoaded)
            {
                DEBUG_PRINT("\n Pause --> loaded \n");
                this->m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                    OMX_EventError, OMX_ErrorIncorrectStateTransition,
                    0, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            } else if (eState == OMX_StateWaitForResources)
            {
                DEBUG_PRINT("\n Pause --> WaitForResources \n");
                this->m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                    OMX_EventError, OMX_ErrorIncorrectStateTransition,
                    0, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            } else if (eState == OMX_StatePause)
            {
                DEBUG_PRINT("\n Pause --> Pause \n");
                this->m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                    OMX_EventError, OMX_ErrorSameState,
                    0, NULL );
                eRet = OMX_ErrorSameState;
            } else if (eState == OMX_StateInvalid)
            {
                DEBUG_PRINT("\n Pause --> Invalid \n");
                m_state = OMX_StateInvalid;
                this->m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                    OMX_EventError, OMX_ErrorInvalidState,
                    0, NULL );
                eRet = OMX_ErrorInvalidState;
            } else
            {
                DEBUG_PRINT("SCP-->Paused to %d Not Handled\n",eState);
                eRet = OMX_ErrorBadParameter;
            }
        }
        /**************************************/
        /* Current State is WaitForResources  */
        /**************************************/
        else if (m_state == OMX_StateWaitForResources)
        {
            if (eState == OMX_StateLoaded)
            {
                DEBUG_PRINT("OMXCORE-SM: WaitForResources-->Loaded\n");
            } else if (eState == OMX_StateWaitForResources)
            {
                DEBUG_PRINT("OMXCORE-SM: WaitForResources-->WaitForResources\n");
                this->m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                    OMX_EventError, OMX_ErrorSameState,
                    0, NULL );
                eRet = OMX_ErrorSameState;
            } else if (eState == OMX_StateExecuting)
            {
                DEBUG_PRINT("OMXCORE-SM: WaitForResources-->Executing\n");
                this->m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                    OMX_EventError,
                    OMX_ErrorIncorrectStateTransition,
                    0, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            } else if (eState == OMX_StatePause)
            {
                DEBUG_PRINT("OMXCORE-SM: WaitForResources-->Pause\n");
                this->m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                    OMX_EventError,
                    OMX_ErrorIncorrectStateTransition,
                    0, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            } else if (eState == OMX_StateInvalid)
            {
                DEBUG_PRINT("OMXCORE-SM: WaitForResources-->Invalid\n");
                m_state = OMX_StateInvalid;
                this->m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                    OMX_EventError,
                    OMX_ErrorInvalidState,
                    0, NULL );
                eRet = OMX_ErrorInvalidState;
            } else
            {
                DEBUG_PRINT_ERROR("SCP--> %d to %d(Not Handled)\n",m_state,eState);
                eRet = OMX_ErrorBadParameter;
            }
        }
        /****************************/
        /* Current State is Invalid */
        /****************************/
        else if (m_state == OMX_StateInvalid)
        {
            if (OMX_StateLoaded == eState || OMX_StateWaitForResources == eState
                || OMX_StateIdle == eState || OMX_StateExecuting == eState
                || OMX_StatePause == eState || OMX_StateInvalid == eState)
            {
                DEBUG_PRINT("OMXCORE-SM: Invalid-->Loaded/Idle/Executing/Pause"
                    "/Invalid/WaitForResources\n");
                m_state = OMX_StateInvalid;
                this->m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                    OMX_EventError, OMX_ErrorInvalidState,
                    0, NULL );
                eRet = OMX_ErrorInvalidState;
            }
        } else
        {
            DEBUG_PRINT_ERROR("OMXCORE-SM: %d --> %d(Not Handled)\n",\
                m_state,eState);
            eRet = OMX_ErrorBadParameter;
        }
    } else if (OMX_CommandFlush == cmd)
    {
        DEBUG_DETAIL("*************************\n");
        DEBUG_PRINT("SCP-->RXED FLUSH COMMAND port=%d\n",param1);
        DEBUG_DETAIL("*************************\n");
        bFlag = 0;
        if (param1 == OMX_CORE_INPUT_PORT_INDEX ||
            param1 == OMX_CORE_OUTPUT_PORT_INDEX ||
            param1 == -1)
        {
            execute_omx_flush(param1);
        } else
        {
            eRet = OMX_ErrorBadPortIndex;
            m_cb.EventHandler(&m_cmp, m_app_data, OMX_EventError,
                OMX_CommandFlush, OMX_ErrorBadPortIndex, NULL );
        }
    } else if (cmd == OMX_CommandPortDisable)
    {
        if (param1 == OMX_CORE_INPUT_PORT_INDEX || param1 == OMX_ALL)
        {
            pcm_input =0;
            DEBUG_PRINT("SCP: Disabling Input port Indx\n");
            m_inp_bEnabled = OMX_FALSE;
            if ((m_state == OMX_StateLoaded || m_state == OMX_StateIdle)
                && release_done(0))
            {
                DEBUG_PRINT("send_command_proxy:OMX_CommandPortDisable:\
                            OMX_CORE_INPUT_PORT_INDEX:release_done \n");
                DEBUG_PRINT("************* OMX_CommandPortDisable:\
                            m_inp_bEnabled********\n",m_inp_bEnabled);

                post_command(OMX_CommandPortDisable,
                    OMX_CORE_INPUT_PORT_INDEX,
                    OMX_COMPONENT_GENERATE_EVENT);
            }

            else
            {
                if (m_state == OMX_StatePause ||m_state == OMX_StateExecuting)
                {
                    DEBUG_PRINT("SCP: execute_omx_flush in Disable in "\
                        " param1=%d m_state=%d \n",param1, m_state);
                    execute_omx_flush(param1);
                }
                DEBUG_PRINT("send_command_proxy:OMX_CommandPortDisable:\
                            OMX_CORE_INPUT_PORT_INDEX \n");
                BITMASK_SET(&m_flags, OMX_COMPONENT_INPUT_DISABLE_PENDING);
                // Skip the event notification
                bFlag = 0;
            }

        }
        if (param1 == OMX_CORE_OUTPUT_PORT_INDEX || param1 == OMX_ALL)
        {
            DEBUG_PRINT("SCP: Disabling Output port Indx\n");
            m_out_bEnabled = OMX_FALSE;
            if ((m_state == OMX_StateLoaded || m_state == OMX_StateIdle)
                && release_done(1))
            {
                DEBUG_PRINT("send_command_proxy:OMX_CommandPortDisable:\
                            OMX_CORE_OUTPUT_PORT_INDEX:release_done \n");
                DEBUG_PRINT("************* OMX_CommandPortDisable:\
                            m_out_bEnabled********\n",m_inp_bEnabled);

                post_command(OMX_CommandPortDisable,
                    OMX_CORE_OUTPUT_PORT_INDEX,
                    OMX_COMPONENT_GENERATE_EVENT);
            } else
            {
                if (m_state == OMX_StatePause ||m_state == OMX_StateExecuting)
                {
                    DEBUG_PRINT("SCP: execute_omx_flush in Disable out "\
                        "param1=%d m_state=%d \n",param1, m_state);
                    execute_omx_flush(param1);
                }
                BITMASK_SET(&m_flags, OMX_COMPONENT_OUTPUT_DISABLE_PENDING);
                // Skip the event notification
                bFlag = 0;
            }
        } else
        {
            DEBUG_PRINT_ERROR("OMX_CommandPortDisable: disable wrong port ID");
        }

    } else if (cmd == OMX_CommandPortEnable)
    {
        if (param1 == OMX_CORE_INPUT_PORT_INDEX  || param1 == OMX_ALL)
        {
            pcm_input = 1;
            m_inp_bEnabled = OMX_TRUE;
            DEBUG_PRINT("SCP: Enabling Input port Indx\n");
            if ((m_state == OMX_StateLoaded
                && !BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING))
                || (m_state == OMX_StateWaitForResources)
                || (m_inp_bPopulated == OMX_TRUE))
            {
                post_command(OMX_CommandPortEnable,
                    OMX_CORE_INPUT_PORT_INDEX,
                    OMX_COMPONENT_GENERATE_EVENT);

            } else
            {
                BITMASK_SET(&m_flags, OMX_COMPONENT_INPUT_ENABLE_PENDING);
                // Skip the event notification
                bFlag = 0;
            }
        }

        if (param1 == OMX_CORE_OUTPUT_PORT_INDEX || param1 == OMX_ALL)
        {
            DEBUG_PRINT("SCP: Enabling Output port Indx\n");
            m_out_bEnabled = OMX_TRUE;
            if ((m_state == OMX_StateLoaded
                && !BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING))
                || (m_state == OMX_StateWaitForResources)
                || (m_out_bPopulated == OMX_TRUE))
            {
                post_command(OMX_CommandPortEnable,
                    OMX_CORE_OUTPUT_PORT_INDEX,
                    OMX_COMPONENT_GENERATE_EVENT);
            } else
            {
                DEBUG_PRINT("send_command_proxy:OMX_CommandPortEnable:\
                            OMX_CORE_OUTPUT_PORT_INDEX:release_done \n");
                BITMASK_SET(&m_flags, OMX_COMPONENT_OUTPUT_ENABLE_PENDING);
                // Skip the event notification
                bFlag = 0;
            }
            pthread_mutex_lock(&m_out_th_lock_1);
            if (is_out_th_sleep)
            {
                is_out_th_sleep = false;
                DEBUG_PRINT("SCP:WAKING OUT THR, OMX_CommandPortEnable\n");
                out_th_wakeup();
            }
            pthread_mutex_unlock(&m_out_th_lock_1);
        } else
        {
            DEBUG_PRINT_ERROR("OMX_CommandPortEnable: disable wrong port ID");
        }

    } else
    {
        DEBUG_PRINT_ERROR("SCP-->ERROR: Invali Command [%d]\n",cmd);
        eRet = OMX_ErrorNotImplemented;
    }
    if(!m_idle_transition)
    {
        DEBUG_PRINT("posting sem_States\n");
        sem_post (&sem_States);
    }
    else
    {
        m_idle_transition = 0;
    }
    if (eRet == OMX_ErrorNone && bFlag)
    {
        post_command(cmd,eState,OMX_COMPONENT_GENERATE_EVENT);
    }
    return eRet;
}

/*=============================================================================
FUNCTION:
  execute_omx_flush

DESCRIPTION:
  Function that flushes buffers that are pending to be written to driver

INPUT/OUTPUT PARAMETERS:
  [IN] param1
  [IN] cmd_cmpl

RETURN VALUE:
  true
  false

Dependency:
  None

SIDE EFFECTS:
  None
=============================================================================*/
bool omx_aac_aenc::execute_omx_flush(OMX_IN OMX_U32 param1, bool cmd_cmpl)
{
    bool bRet = true;
    OMX_BUFFERHEADERTYPE *omx_buf;
    unsigned      p1;                            // Parameter - 1
    unsigned      p2;                            // Parameter - 2
    unsigned      ident;
    bool          bqueStatus = 0;

    DEBUG_PRINT("Execute_omx_flush Port[%d]", param1);
    struct timespec abs_timeout;
    abs_timeout.tv_sec = 1;
    abs_timeout.tv_nsec = 0;                     //333333;

    if (param1 == -1)
    {
        DEBUG_PRINT("Execute flush for both I/p O/p port\n");
        pthread_mutex_lock(&m_flush_lock);
        m_flush_cnt = 2;
        pthread_mutex_unlock(&m_flush_lock);

        // Send Flush commands to input and output threads
        post_input(OMX_CommandFlush,
            OMX_CORE_INPUT_PORT_INDEX,OMX_COMPONENT_GENERATE_COMMAND);
        post_output(OMX_CommandFlush,
            OMX_CORE_OUTPUT_PORT_INDEX,OMX_COMPONENT_GENERATE_COMMAND);
        // Send Flush to the kernel so that the in and out buffers are released
        if (ioctl( m_drv_fd, AUDIO_FLUSH, 0) == -1)
            DEBUG_PRINT("FLush:ioctl flush failed errno=%d\n",errno);
        DEBUG_DETAIL("****************************************");
        DEBUG_DETAIL("is_in_th_sleep=%d is_out_th_sleep=%d\n",\
            is_in_th_sleep,is_out_th_sleep);
        DEBUG_DETAIL("****************************************");

        pthread_mutex_lock(&m_in_th_lock_1);
        if (is_in_th_sleep)
        {
            is_in_th_sleep = false;
            DEBUG_DETAIL("For FLUSH-->WAKING UP IN THREADS\n");
            in_th_wakeup();
        }
        pthread_mutex_unlock(&m_in_th_lock_1);

        pthread_mutex_lock(&m_out_th_lock_1);
        if (is_out_th_sleep)
        {
            is_out_th_sleep = false;
            DEBUG_DETAIL("For FLUSH-->WAKING UP OUT THREADS\n");
            out_th_wakeup();
        }
        pthread_mutex_unlock(&m_out_th_lock_1);


        while (1 )
        {
            pthread_mutex_lock(&out_buf_count_lock);
            pthread_mutex_lock(&in_buf_count_lock);
            DEBUG_PRINT("Flush:nNumOutputBuf = %d nNumInputBuf=%d\n",\
                nNumOutputBuf,nNumInputBuf);

            if (nNumOutputBuf > 0 || nNumInputBuf > 0)
            {
                pthread_mutex_unlock(&in_buf_count_lock);
                pthread_mutex_unlock(&out_buf_count_lock);
                DEBUG_PRINT(" READ FLUSH PENDING HENCE WAIT\n");
                DEBUG_PRINT("BEFORE READ ioctl_flush\n");
                usleep (10000);
                if (ioctl( m_drv_fd, AUDIO_FLUSH, 0) == -1)
                    DEBUG_PRINT("Flush: ioctl flush failed %d\n",\
                    errno);
                DEBUG_PRINT("AFTER READ ioctl_flush\n");
                sem_timedwait(&sem_read_msg,&abs_timeout);
                DEBUG_PRINT("AFTER READ SEM_TIMEWAIT\n");
            } else
            {
                pthread_mutex_unlock(&in_buf_count_lock);
                pthread_mutex_unlock(&out_buf_count_lock);
                break;
            }
        }

        // sleep till the FLUSH ACK are done by both the input and
        // output threads
        DEBUG_DETAIL("WAITING FOR FLUSH ACK's param1=%d",param1);
        wait_for_event();

        DEBUG_PRINT("RECIEVED BOTH FLUSH ACK's param1=%d cmd_cmpl=%d",\
            param1,cmd_cmpl);

        // If not going to idle state, Send FLUSH complete message to the Client,
        // now that FLUSH ACK's have been recieved.
        if (cmd_cmpl)
        {
            m_cb.EventHandler(&m_cmp, m_app_data, OMX_EventCmdComplete,
                OMX_CommandFlush, OMX_CORE_INPUT_PORT_INDEX, NULL );
            m_cb.EventHandler(&m_cmp, m_app_data, OMX_EventCmdComplete,
                OMX_CommandFlush, OMX_CORE_OUTPUT_PORT_INDEX, NULL );
            DEBUG_PRINT("Inside FLUSH.. sending FLUSH CMPL\n");
        }
    } else if (OMX_CORE_INPUT_PORT_INDEX == param1)
    {
        DEBUG_PRINT("Execute FLUSH for I/p port\n");
        pthread_mutex_lock(&m_flush_lock);
        m_flush_cnt = 1;
        pthread_mutex_unlock(&m_flush_lock);
        post_input(OMX_CommandFlush,
            OMX_CORE_INPUT_PORT_INDEX,OMX_COMPONENT_GENERATE_COMMAND);
        if (ioctl( m_drv_fd, AUDIO_FLUSH, 0) == -1)
            DEBUG_PRINT("Flush:Input port, ioctl flush failed %d\n",errno);
        DEBUG_DETAIL("****************************************");
        DEBUG_DETAIL("is_in_th_sleep=%d\n",\
            is_in_th_sleep);
        DEBUG_DETAIL("****************************************");

        if (is_in_th_sleep)
        {
            pthread_mutex_lock(&m_in_th_lock_1);
            is_in_th_sleep = false;
            pthread_mutex_unlock(&m_in_th_lock_1);
            DEBUG_DETAIL("For FLUSH-->WAKING UP IN THREADS\n");
            in_th_wakeup();
        }

        //sleep till the FLUSH ACK are done by both the input and output threads
        DEBUG_DETAIL("Executing FLUSH for I/p port\n");
        DEBUG_DETAIL("WAITING FOR FLUSH ACK's param1=%d",param1);
        wait_for_event();
        DEBUG_DETAIL(" RECIEVED FLUSH ACK FOR I/P PORT param1=%d",param1);

        // Send FLUSH complete message to the Client,
        // now that FLUSH ACK's have been recieved.
        if (cmd_cmpl)
        {
            m_cb.EventHandler(&m_cmp, m_app_data, OMX_EventCmdComplete,
                OMX_CommandFlush, OMX_CORE_INPUT_PORT_INDEX, NULL );
        }
    } else if (OMX_CORE_OUTPUT_PORT_INDEX == param1)
    {
        DEBUG_PRINT("Executing FLUSH for O/p port\n");
        pthread_mutex_lock(&m_flush_lock);
        m_flush_cnt = 1;
        pthread_mutex_unlock(&m_flush_lock);
        DEBUG_DETAIL("Executing FLUSH for O/p port\n");
        DEBUG_DETAIL("WAITING FOR FLUSH ACK's param1=%d",param1);
        post_output(OMX_CommandFlush,
            OMX_CORE_OUTPUT_PORT_INDEX,OMX_COMPONENT_GENERATE_COMMAND);
        if (ioctl( m_drv_fd, AUDIO_FLUSH, 0) ==-1)
            DEBUG_PRINT("Flush:Output port, ioctl flush failed %d\n",errno);
        DEBUG_DETAIL("****************************************");
        DEBUG_DETAIL("is_out_th_sleep=%d\n",\
            is_out_th_sleep);
        DEBUG_DETAIL("****************************************");
        if (is_out_th_sleep)
        {
            pthread_mutex_lock(&m_out_th_lock_1);
            is_out_th_sleep = false;
            pthread_mutex_unlock(&m_out_th_lock_1);
            DEBUG_DETAIL("For FLUSH-->WAKING UP OUT THREADS\n");
            out_th_wakeup();
        }

        // sleep till the FLUSH ACK are done by utput thread
        wait_for_event();
        // Send FLUSH complete message to the Client,
        // now that FLUSH ACK's have been recieved.
        if (cmd_cmpl)
        {
            m_cb.EventHandler(&m_cmp, m_app_data, OMX_EventCmdComplete,
                OMX_CommandFlush, OMX_CORE_OUTPUT_PORT_INDEX, NULL );
        }
        DEBUG_DETAIL("RECIEVED FLUSH ACK FOR O/P PORT param1=%d",param1);
    } else
    {
        DEBUG_PRINT("Invalid Port ID[%d]",param1);
    }
    return bRet;
}

/*=============================================================================
FUNCTION:
  execute_input_omx_flush

DESCRIPTION:
  Function that flushes buffers that are pending to be written to driver

INPUT/OUTPUT PARAMETERS:
  None

RETURN VALUE:
  true
  false

Dependency:
  None

SIDE EFFECTS:
  None
=============================================================================*/
bool omx_aac_aenc::execute_input_omx_flush()
{
    OMX_BUFFERHEADERTYPE *omx_buf;
    unsigned      p1;                            // Parameter - 1
    unsigned      p2;                            // Parameter - 2
    unsigned      ident;
    unsigned      qsize=0;                       // qsize
    unsigned      tot_qsize=0;                   // qsize

    DEBUG_PRINT("Execute_omx_flush on input port");

    pthread_mutex_lock(&m_lock);
    do
    {
        qsize = m_input_q.m_size;
        tot_qsize = qsize;
        tot_qsize += m_input_ctrl_ebd_q.m_size;

        DEBUG_DETAIL("Input FLUSH-->flushq[%d] ebd[%d]dataq[%d]",\
            m_input_ctrl_cmd_q.m_size,
            m_input_ctrl_ebd_q.m_size,qsize);
        if (!tot_qsize)
        {
            DEBUG_DETAIL("Input-->BREAKING FROM execute_input_flush LOOP");
            pthread_mutex_unlock(&m_lock);
            break;
        }
        if (qsize)
        {
            m_input_q.pop_entry(&p1, &p2, &ident);
            if ((ident == OMX_COMPONENT_GENERATE_ETB) ||
                (ident == OMX_COMPONENT_GENERATE_BUFFER_DONE))
            {
                omx_buf = (OMX_BUFFERHEADERTYPE *) p2;
                DEBUG_DETAIL("Flush:Input dataq=0x%x \n", omx_buf);
                omx_buf->nFilledLen = 0;
                buffer_done_cb((OMX_BUFFERHEADERTYPE *)omx_buf);
            }
        } else if (m_input_ctrl_ebd_q.m_size)
        {
            m_input_ctrl_ebd_q.pop_entry(&p1, &p2, &ident);
            if (ident == OMX_COMPONENT_GENERATE_BUFFER_DONE)
            {
                omx_buf = (OMX_BUFFERHEADERTYPE *) p2;
                omx_buf->nFilledLen = 0;
                DEBUG_DETAIL("Flush:ctrl dataq=0x%x \n", omx_buf);
                buffer_done_cb((OMX_BUFFERHEADERTYPE *)omx_buf);
            }
        } else
        {
        }
    }while (tot_qsize>0);
    DEBUG_DETAIL("*************************\n");
    DEBUG_DETAIL("IN-->FLUSHING DONE\n");
    DEBUG_DETAIL("*************************\n");
    flush_ack();
    pthread_mutex_unlock(&m_lock);
    return true;
}

/*=============================================================================
FUNCTION:
  execute_output_omx_flush

DESCRIPTION:
  Function that flushes buffers that are pending to be written to driver

INPUT/OUTPUT PARAMETERS:
  None

RETURN VALUE:
  true
  false

Dependency:
  None

SIDE EFFECTS:
  None
=============================================================================*/
bool omx_aac_aenc::execute_output_omx_flush()
{
    OMX_BUFFERHEADERTYPE *omx_buf;
    unsigned      p1;                            // Parameter - 1
    unsigned      p2;                            // Parameter - 2
    unsigned      ident;
    unsigned      qsize=0;                       // qsize
    unsigned      tot_qsize=0;                   // qsize

    DEBUG_PRINT("Execute_omx_flush on output port");

    pthread_mutex_lock(&m_outputlock);
    do
    {
        qsize = m_output_q.m_size;
        DEBUG_DETAIL("OUT FLUSH-->flushq[%d] fbd[%d]dataq[%d]",\
            m_output_ctrl_cmd_q.m_size,
            m_output_ctrl_fbd_q.m_size,qsize);
        tot_qsize = qsize;
        tot_qsize += m_output_ctrl_fbd_q.m_size;
        if (!tot_qsize)
        {
            DEBUG_DETAIL("OUT-->BREAKING FROM execute_input_flush LOOP");
            pthread_mutex_unlock(&m_outputlock);
            break;
        }
        if (qsize)
        {
            m_output_q.pop_entry(&p1,&p2,&ident);
            if ( (OMX_COMPONENT_GENERATE_FTB == ident) ||
                (OMX_COMPONENT_GENERATE_FRAME_DONE == ident))
            {
                omx_buf = (OMX_BUFFERHEADERTYPE *) p2;
                DEBUG_DETAIL("Ouput Buf_Addr=%x TS[0x%x] \n",\
                    omx_buf,nTimestamp);
                omx_buf->nTimeStamp = nTimestamp;
                omx_buf->nFilledLen = 0;
                frame_done_cb((OMX_BUFFERHEADERTYPE *)omx_buf);
                DEBUG_DETAIL("CALLING FBD FROM FLUSH");
            }
        } else if ((qsize = m_output_ctrl_fbd_q.m_size))
        {
            m_output_ctrl_fbd_q.pop_entry(&p1, &p2, &ident);
            if (OMX_COMPONENT_GENERATE_FRAME_DONE == ident)
            {
                omx_buf = (OMX_BUFFERHEADERTYPE *) p2;
                DEBUG_DETAIL("Ouput Buf_Addr=%x TS[0x%x] \n", \
                    omx_buf,nTimestamp);
                omx_buf->nTimeStamp = nTimestamp;
                omx_buf->nFilledLen = 0;
                frame_done_cb((OMX_BUFFERHEADERTYPE *)omx_buf);
                DEBUG_DETAIL("CALLING FROM CTRL-FBDQ FROM FLUSH");
            }
        }
    }while (qsize>0);
    DEBUG_DETAIL("*************************\n");
    DEBUG_DETAIL("OUT-->FLUSHING DONE\n");
    DEBUG_DETAIL("*************************\n");
    flush_ack();
    pthread_mutex_unlock(&m_outputlock);
    return true;
}

/*=============================================================================
FUNCTION:
  post_input

DESCRIPTION:
  Function that posts command in the command queue

INPUT/OUTPUT PARAMETERS:
  [IN] p1
  [IN] p2
  [IN] id - command ID
  [IN] lock - self-locking mode

RETURN VALUE:
  true
  false

Dependency:
  None

SIDE EFFECTS:
  None
=============================================================================*/
bool omx_aac_aenc::post_input(unsigned int p1,
                               unsigned int p2,
                               unsigned int id)
{
    bool bRet = false;
    pthread_mutex_lock(&m_lock);

    if ((OMX_COMPONENT_GENERATE_COMMAND == id))
    {
        // insert flush message and ebd
        m_input_ctrl_cmd_q.insert_entry(p1,p2,id);
    } else if ((OMX_COMPONENT_GENERATE_BUFFER_DONE == id))
    {
        // insert ebd
        m_input_ctrl_ebd_q.insert_entry(p1,p2,id);
    } else
    {
        // ETBS in this queue
        m_input_q.insert_entry(p1,p2,id);
    }

    if (m_ipc_to_in_th)
    {
        bRet = true;
        omx_aac_post_msg(m_ipc_to_in_th, id);
    }

    DEBUG_DETAIL("PostInput-->state[%d]id[%d]flushq[%d]ebdq[%d]dataq[%d] \n",\
        m_state,
        id,
        m_input_ctrl_cmd_q.m_size,
        m_input_ctrl_ebd_q.m_size,
        m_input_q.m_size);

    pthread_mutex_unlock(&m_lock);
    return bRet;
}

/*=============================================================================
FUNCTION:
  post_command

DESCRIPTION:
  Function that posts command in the command queue

INPUT/OUTPUT PARAMETERS:
  [IN] p1
  [IN] p2
  [IN] id - command ID
  [IN] lock - self-locking mode

RETURN VALUE:
  true
  false

Dependency:
  None

SIDE EFFECTS:
  None
=============================================================================*/
bool omx_aac_aenc::post_command(unsigned int p1,
                                 unsigned int p2,
                                 unsigned int id)
{
    bool bRet  = false;

    pthread_mutex_lock(&m_commandlock);

    m_command_q.insert_entry(p1,p2,id);

    if (m_ipc_to_cmd_th)
    {
        bRet = true;
        omx_aac_post_msg(m_ipc_to_cmd_th, id);
    }

    DEBUG_DETAIL("PostCmd-->state[%d]id[%d]cmdq[%d]flags[%x]\n",\
        m_state,
        id,
        m_command_q.m_size,
        m_flags >> 3);

    pthread_mutex_unlock(&m_commandlock);
    return bRet;
}

/*=============================================================================
FUNCTION:
  post_output

DESCRIPTION:
  Function that posts command in the command queue

INPUT/OUTPUT PARAMETERS:
  [IN] p1
  [IN] p2
  [IN] id - command ID
  [IN] lock - self-locking mode

RETURN VALUE:
  true
  false

Dependency:
  None

SIDE EFFECTS:
  None
=============================================================================*/
bool omx_aac_aenc::post_output(unsigned int p1,
                                unsigned int p2,
                                unsigned int id)
{
    bool bRet = false;

    pthread_mutex_lock(&m_outputlock);
    if ((OMX_COMPONENT_GENERATE_COMMAND == id) )
    {
        // insert flush message and fbd
        m_output_ctrl_cmd_q.insert_entry(p1,p2,id);
    } else if ((OMX_COMPONENT_GENERATE_FRAME_DONE == id) )
    {
        // insert flush message and fbd
        m_output_ctrl_fbd_q.insert_entry(p1,p2,id);
    } else
    {
        m_output_q.insert_entry(p1,p2,id);
    }

    if (m_ipc_to_out_th)
    {
        bRet = true;
        omx_aac_post_msg(m_ipc_to_out_th, id);
    }
    DEBUG_DETAIL("PostOutput-->state[%d]id[%d]flushq[%d]ebdq[%d]dataq[%d]\n",\
        m_state,
        id,
        m_output_ctrl_cmd_q.m_size,
        m_output_ctrl_fbd_q.m_size,
        m_output_q.m_size);

    pthread_mutex_unlock(&m_outputlock);
    return bRet;
}
/**
  @brief member function that return parameters to IL client

  @param hComp handle to component instance
  @param paramIndex Parameter type
  @param paramData pointer to memory space which would hold the
        paramter
  @return error status
*/
OMX_ERRORTYPE  omx_aac_aenc::get_parameter(OMX_IN OMX_HANDLETYPE     hComp,
                                            OMX_IN OMX_INDEXTYPE paramIndex,
                                            OMX_INOUT OMX_PTR     paramData)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    if (m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Get Param in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    if (paramData == NULL)
    {
        DEBUG_PRINT("get_parameter: paramData is NULL\n");
        return OMX_ErrorBadParameter;
    }

    switch (paramIndex)
    {
    case OMX_IndexParamPortDefinition:
        {
            OMX_PARAM_PORTDEFINITIONTYPE *portDefn;
            portDefn = (OMX_PARAM_PORTDEFINITIONTYPE *) paramData;

            DEBUG_PRINT("OMX_IndexParamPortDefinition " \
                "portDefn->nPortIndex = %d\n",portDefn->nPortIndex);

            portDefn->nVersion.nVersion = OMX_SPEC_VERSION;
            portDefn->nSize = sizeof(portDefn);
            portDefn->eDomain    = OMX_PortDomainAudio;

            if (0 == portDefn->nPortIndex)
            {
                portDefn->eDir       = OMX_DirInput;
                portDefn->bEnabled   = m_inp_bEnabled;
                portDefn->bPopulated = m_inp_bPopulated;
                portDefn->nBufferCountActual = m_inp_act_buf_count;
                portDefn->nBufferCountMin    = OMX_CORE_NUM_INPUT_BUFFERS;
                portDefn->nBufferSize        = OMX_CORE_INPUT_BUFFER_SIZE;
                input_buffer_size = OMX_CORE_INPUT_BUFFER_SIZE;
                portDefn->format.audio.bFlagErrorConcealment = OMX_TRUE;
                if (portDefn->format.audio.cMIMEType != NULL)
                {
                    char mime_type[] = "audio/aac";
                    portDefn->format.audio.cMIMEType =
                        (OMX_STRING)malloc(sizeof(mime_type));
                    memcpy(portDefn->format.audio.cMIMEType,
                        mime_type, sizeof(mime_type));
                }
                portDefn->format.audio.eEncoding = OMX_AUDIO_CodingPCM;
                portDefn->format.audio.pNativeRender = 0;
            } else if (1 == portDefn->nPortIndex)
            {
                portDefn->eDir =  OMX_DirOutput;
                portDefn->bEnabled   = m_out_bEnabled;
                portDefn->bPopulated = m_out_bPopulated;
                portDefn->nBufferCountActual = m_out_act_buf_count;
                portDefn->nBufferCountMin    = OMX_CORE_NUM_OUTPUT_BUFFERS;
                portDefn->nBufferSize        = OMX_AAC_OUTPUT_BUFFER_SIZE;
                output_buffer_size   = OMX_AAC_OUTPUT_BUFFER_SIZE;
                portDefn->format.audio.bFlagErrorConcealment = OMX_TRUE;
                portDefn->format.audio.eEncoding = OMX_AUDIO_CodingAAC;
                portDefn->format.audio.pNativeRender = 0;
            } else
            {
                portDefn->eDir =  OMX_DirMax;
                DEBUG_PRINT_ERROR("Bad Port idx %d\n",\
                    (int)portDefn->nPortIndex);
                eRet = OMX_ErrorBadPortIndex;
            }
            break;
        }

    case OMX_IndexParamAudioInit:
        {
            OMX_PORT_PARAM_TYPE *portParamType =
                (OMX_PORT_PARAM_TYPE *) paramData;
            DEBUG_PRINT("OMX_IndexParamAudioInit\n");

            portParamType->nVersion.nVersion = OMX_SPEC_VERSION;
            portParamType->nSize = sizeof(portParamType);
            portParamType->nPorts           = 2;
            portParamType->nStartPortNumber = 0;
            break;
        }

    case OMX_IndexParamAudioPortFormat:
        {
            OMX_AUDIO_PARAM_PORTFORMATTYPE *portFormatType =
                (OMX_AUDIO_PARAM_PORTFORMATTYPE *) paramData;
            DEBUG_PRINT("OMX_IndexParamAudioPortFormat\n");
            portFormatType->nVersion.nVersion = OMX_SPEC_VERSION;
            portFormatType->nSize = sizeof(portFormatType);

            if (OMX_CORE_INPUT_PORT_INDEX == portFormatType->nPortIndex)
            {
                portFormatType->eEncoding = OMX_AUDIO_CodingPCM;
            } else if (OMX_CORE_OUTPUT_PORT_INDEX== portFormatType->nPortIndex)
            {
                DEBUG_PRINT("get_parameter: OMX_IndexParamAudioFormat: "\
                    "%d\n", portFormatType->nIndex);
                portFormatType->eEncoding = OMX_AUDIO_CodingAAC;
            } else
            {
                DEBUG_PRINT_ERROR("get_parameter: Bad port index %d\n", (int)portFormatType->nPortIndex);
                eRet = OMX_ErrorBadPortIndex;
            }
            break;
        }

    case OMX_IndexParamAudioAac:
        {
            OMX_AUDIO_PARAM_AACPROFILETYPE *aacParam = (OMX_AUDIO_PARAM_AACPROFILETYPE *) paramData;
            DEBUG_PRINT("OMX_IndexParamAudioAac\n");
            memcpy(aacParam,&m_aac_param, sizeof(OMX_AUDIO_PARAM_AACPROFILETYPE));
            break;
        }

    case OMX_IndexParamVideoInit:
        {
            OMX_PORT_PARAM_TYPE *portParamType = (OMX_PORT_PARAM_TYPE *) paramData;
            DEBUG_PRINT("get_parameter: OMX_IndexParamVideoInit\n");
            portParamType->nVersion.nVersion = OMX_SPEC_VERSION;
            portParamType->nSize = sizeof(portParamType);
            portParamType->nPorts           = 0;
            portParamType->nStartPortNumber = 0;
            break;
        }
    case OMX_IndexParamPriorityMgmt:
        {
            OMX_PRIORITYMGMTTYPE *priorityMgmtType =
                (OMX_PRIORITYMGMTTYPE*)paramData;
            DEBUG_PRINT("get_parameter: OMX_IndexParamPriorityMgmt\n");
            priorityMgmtType->nSize = sizeof(priorityMgmtType);
            priorityMgmtType->nVersion.nVersion = OMX_SPEC_VERSION;
            priorityMgmtType->nGroupID = m_priority_mgm.nGroupID;
            priorityMgmtType->nGroupPriority = m_priority_mgm.nGroupPriority;
            break;
        }
    case OMX_IndexParamImageInit:
        {
            OMX_PORT_PARAM_TYPE *portParamType =
                (OMX_PORT_PARAM_TYPE *) paramData;
            DEBUG_PRINT("get_parameter: OMX_IndexParamImageInit\n");
            portParamType->nVersion.nVersion = OMX_SPEC_VERSION;
            portParamType->nSize = sizeof(portParamType);
            portParamType->nPorts           = 0;
            portParamType->nStartPortNumber = 0;
            break;
        }

    case OMX_IndexParamCompBufferSupplier:
        {
            DEBUG_PRINT("get_parameter: OMX_IndexParamCompBufferSupplier\n");
            OMX_PARAM_BUFFERSUPPLIERTYPE *bufferSupplierType
                = (OMX_PARAM_BUFFERSUPPLIERTYPE*) paramData;
            DEBUG_PRINT("get_parameter: OMX_IndexParamCompBufferSupplier\n");

            bufferSupplierType->nSize = sizeof(bufferSupplierType);
            bufferSupplierType->nVersion.nVersion = OMX_SPEC_VERSION;
            if (OMX_CORE_INPUT_PORT_INDEX   == bufferSupplierType->nPortIndex)
            {
                bufferSupplierType->nPortIndex = OMX_BufferSupplyUnspecified;
            } else if (OMX_CORE_OUTPUT_PORT_INDEX == bufferSupplierType->nPortIndex)
            {
                bufferSupplierType->nPortIndex = OMX_BufferSupplyUnspecified;
            } else
            {
                eRet = OMX_ErrorBadPortIndex;
            }
            DEBUG_PRINT_ERROR("get_parameter:"\
                "OMX_IndexParamCompBufferSupplier eRet"\
                "%08x\n", eRet);
            break;
        }

        /*Component should support this port definition*/
    case OMX_IndexParamOtherInit:
        {
            OMX_PORT_PARAM_TYPE *portParamType = (OMX_PORT_PARAM_TYPE *) paramData;
            DEBUG_PRINT("get_parameter: OMX_IndexParamOtherInit\n");
            portParamType->nVersion.nVersion = OMX_SPEC_VERSION;
            portParamType->nSize = sizeof(portParamType);
            portParamType->nPorts           = 0;
            portParamType->nStartPortNumber = 0;
            break;
        }
    default:
        {
            DEBUG_PRINT_ERROR("unknown param %08x\n", paramIndex);
            eRet = OMX_ErrorUnsupportedIndex;
        }
    }
    return eRet;

}

/**
 @brief member function that set paramter from IL client

 @param hComp handle to component instance
 @param paramIndex parameter type
 @param paramData pointer to memory space which holds the paramter
 @return error status
 */
OMX_ERRORTYPE  omx_aac_aenc::set_parameter(OMX_IN OMX_HANDLETYPE     hComp,
                                            OMX_IN OMX_INDEXTYPE paramIndex,
                                            OMX_IN OMX_PTR        paramData)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    int           i;
    if (m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Set Param in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    if (paramData == NULL)
    {
        DEBUG_PRINT("param data is NULL");
        return OMX_ErrorBadParameter;
    }

    switch (paramIndex)
    {
    case OMX_IndexParamAudioAac:
        {
            DEBUG_PRINT("OMX_IndexParamAudioAAC");
            memcpy(&m_aac_param,paramData, sizeof(OMX_AUDIO_PARAM_AACPROFILETYPE));

            if(m_aac_param.nChannels == 1)
            {
                frameDuration = (((output_buffer_size)* 1000) /(m_aac_param.nSampleRate * 2));
                DEBUG_PRINT("frame duration of mono config = %d sampling rate = %d \n",
                             frameDuration,m_aac_param.nSampleRate);
            }
            else if(m_aac_param.nChannels == 2)
            {
                frameDuration = (((output_buffer_size)* 1000) /(m_aac_param.nSampleRate  * 4));
                DEBUG_PRINT("frame duration of stero config = %d sampling rate = %d \n",
                             frameDuration,m_aac_param.nSampleRate);
            }
            break;
        }
    case OMX_IndexParamPortDefinition:
        {
            OMX_PARAM_PORTDEFINITIONTYPE *portDefn;
            portDefn = (OMX_PARAM_PORTDEFINITIONTYPE *) paramData;

            if (((m_state == OMX_StateLoaded)&&
                !BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING))
                || (m_state == OMX_StateWaitForResources &&
                ((OMX_DirInput == portDefn->eDir && m_inp_bEnabled == true)||
                (OMX_DirInput == portDefn->eDir && m_out_bEnabled == true)))
                ||(((OMX_DirInput == portDefn->eDir && m_inp_bEnabled == false)||
                (OMX_DirInput == portDefn->eDir && m_out_bEnabled == false)) &&
                (m_state != OMX_StateWaitForResources)))
            {
                DEBUG_PRINT("Set Parameter called in valid state\n");
            } else
            {
                DEBUG_PRINT_ERROR("Set Parameter called in Invalid State\n");
                return OMX_ErrorIncorrectStateOperation;
            }
            DEBUG_PRINT("OMX_IndexParamPortDefinition portDefn->nPortIndex "
                "= %d\n",portDefn->nPortIndex);
            if (OMX_CORE_INPUT_PORT_INDEX == portDefn->nPortIndex)
            {
                if ( portDefn->nBufferCountActual > OMX_CORE_NUM_INPUT_BUFFERS )
                {
                    m_inp_act_buf_count = portDefn->nBufferCountActual;
                } else
                {
                    m_inp_act_buf_count =OMX_CORE_NUM_INPUT_BUFFERS;
                }
                input_buffer_size = portDefn->nBufferSize;

            } else if (OMX_CORE_OUTPUT_PORT_INDEX == portDefn->nPortIndex)
            {
                if ( portDefn->nBufferCountActual > OMX_CORE_NUM_OUTPUT_BUFFERS )
                {
                    m_out_act_buf_count = portDefn->nBufferCountActual;
                } else
                {
                    m_out_act_buf_count =OMX_CORE_NUM_OUTPUT_BUFFERS;
                }
                output_buffer_size = portDefn->nBufferSize;
            } else
            {
                DEBUG_PRINT(" set_parameter: Bad Port idx %d",\
                    (int)portDefn->nPortIndex);
                eRet = OMX_ErrorBadPortIndex;
            }
            break;
        }
    case OMX_IndexParamPriorityMgmt:
        {
            DEBUG_PRINT("set_parameter: OMX_IndexParamPriorityMgmt\n");

            if (m_state != OMX_StateLoaded)
            {
                DEBUG_PRINT_ERROR("Set Parameter called in Invalid State\n");
                return OMX_ErrorIncorrectStateOperation;
            }
            OMX_PRIORITYMGMTTYPE *priorityMgmtype
                = (OMX_PRIORITYMGMTTYPE*) paramData;
            DEBUG_PRINT("set_parameter: OMX_IndexParamPriorityMgmt %d\n",
                priorityMgmtype->nGroupID);

            DEBUG_PRINT("set_parameter: priorityMgmtype %d\n",
                priorityMgmtype->nGroupPriority);

            m_priority_mgm.nGroupID = priorityMgmtype->nGroupID;
            m_priority_mgm.nGroupPriority = priorityMgmtype->nGroupPriority;

            break;
        }
    case  OMX_IndexParamAudioPortFormat:
        {

            OMX_AUDIO_PARAM_PORTFORMATTYPE *portFormatType =
                (OMX_AUDIO_PARAM_PORTFORMATTYPE *) paramData;
            DEBUG_PRINT("set_parameter: OMX_IndexParamAudioPortFormat\n");

            if (OMX_CORE_INPUT_PORT_INDEX== portFormatType->nPortIndex)
            {
                portFormatType->eEncoding = OMX_AUDIO_CodingPCM;
            } else if (OMX_CORE_OUTPUT_PORT_INDEX == portFormatType->nPortIndex)
            {
                DEBUG_PRINT("set_parameter: OMX_IndexParamAudioFormat:"\
                    " %d\n", portFormatType->nIndex);
                portFormatType->eEncoding = OMX_AUDIO_CodingAAC;
            } else
            {
                DEBUG_PRINT_ERROR("set_parameter: Bad port index %d\n", \
                    (int)portFormatType->nPortIndex);
                eRet = OMX_ErrorBadPortIndex;
            }
            break;
        }

    case OMX_IndexParamCompBufferSupplier:
        {
            DEBUG_PRINT("set_parameter: OMX_IndexParamCompBufferSupplier\n");
            OMX_PARAM_BUFFERSUPPLIERTYPE *bufferSupplierType
                = (OMX_PARAM_BUFFERSUPPLIERTYPE*) paramData;
            DEBUG_PRINT("set_param: OMX_IndexParamCompBufferSupplier %d",\
                bufferSupplierType->eBufferSupplier);

            if (bufferSupplierType->nPortIndex == OMX_CORE_INPUT_PORT_INDEX
                || bufferSupplierType->nPortIndex == OMX_CORE_OUTPUT_PORT_INDEX)
            {
                DEBUG_PRINT("set_parameter:OMX_IndexParamCompBufferSupplier\n");
                m_buffer_supplier.eBufferSupplier = bufferSupplierType->eBufferSupplier;
            } else
            {
                eRet = OMX_ErrorBadPortIndex;
            }

            DEBUG_PRINT_ERROR("set_param:IndexParamCompBufferSup %08x\n", \
                eRet);
            break; }

    case OMX_IndexParamAudioPcm:
        {
            DEBUG_PRINT("set_parameter: OMX_IndexParamAudioPcm\n");
            OMX_AUDIO_PARAM_PCMMODETYPE *pcmparam
                = (OMX_AUDIO_PARAM_PCMMODETYPE *) paramData;

            if (OMX_CORE_INPUT_PORT_INDEX== pcmparam->nPortIndex)
            {
                m_aac_param.nChannels=pcmparam->nChannels;

                DEBUG_PRINT("set_parameter: Number of channels %d",\
                    pcmparam->nChannels);
            } else
            {
                DEBUG_PRINT_ERROR("get_parameter:OMX_IndexParamAudioPcm "
                    "OMX_ErrorBadPortIndex %d\n",
                    (int)pcmparam->nPortIndex);
                eRet = OMX_ErrorBadPortIndex;
            }
            break;
        }
    case OMX_IndexParamStandardComponentRole:
        {
            OMX_PARAM_COMPONENTROLETYPE *componentRole;
            componentRole = (OMX_PARAM_COMPONENTROLETYPE*)paramData;
            component_Role.nSize = componentRole->nSize;
            component_Role.nVersion = componentRole->nVersion;
            strcpy((char *)component_Role.cRole,
                (const char*)componentRole->cRole);
            break;
        }

    default:
        {
            DEBUG_PRINT_ERROR("unknown param %d\n", paramIndex);
            eRet = OMX_ErrorUnsupportedIndex;
        }
    }

    return eRet;
}

/* ======================================================================
FUNCTION
  omx_aac_aenc::GetConfig

DESCRIPTION
  OMX Get Config Method implementation.

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if successful.

========================================================================== */
OMX_ERRORTYPE  omx_aac_aenc::get_config(OMX_IN OMX_HANDLETYPE      hComp,
                                         OMX_IN OMX_INDEXTYPE configIndex,
                                         OMX_INOUT OMX_PTR     configData)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    if (m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Get Config in Invalid State\n");
        return OMX_ErrorInvalidState;
    }

    switch (configIndex)
    {
    case OMX_IndexConfigAudioVolume:
        {
            OMX_AUDIO_CONFIG_VOLUMETYPE *volume =
                (OMX_AUDIO_CONFIG_VOLUMETYPE*) configData;

            if (OMX_CORE_INPUT_PORT_INDEX == volume->nPortIndex)
            {
                volume->nSize = sizeof(volume);
                volume->nVersion.nVersion = OMX_SPEC_VERSION;
                volume->bLinear = OMX_TRUE;
                volume->sVolume.nValue = m_volume;
                volume->sVolume.nMax   = OMX_AENC_MAX;
                volume->sVolume.nMin   = OMX_AENC_MIN;
            } else
            {
                eRet = OMX_ErrorBadPortIndex;
            }
        }
        break;

    case OMX_IndexConfigAudioMute:
        {
            OMX_AUDIO_CONFIG_MUTETYPE *mute =
                (OMX_AUDIO_CONFIG_MUTETYPE*) configData;

            if (OMX_CORE_INPUT_PORT_INDEX == mute->nPortIndex)
            {
                mute->nSize = sizeof(mute);
                mute->nVersion.nVersion = OMX_SPEC_VERSION;
                mute->bMute = (BITMASK_PRESENT(&m_flags,
                    OMX_COMPONENT_MUTED)?OMX_TRUE:OMX_FALSE);
            } else
            {
                eRet = OMX_ErrorBadPortIndex;
            }
        }
        break;

    default:
        eRet = OMX_ErrorUnsupportedIndex;
        break;
    }
    return eRet;
}

/* ======================================================================
FUNCTION
  omx_aac_aenc::SetConfig

DESCRIPTION
  OMX Set Config method implementation

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if successful.
========================================================================== */
OMX_ERRORTYPE  omx_aac_aenc::set_config(OMX_IN OMX_HANDLETYPE      hComp,
                                         OMX_IN OMX_INDEXTYPE configIndex,
                                         OMX_IN OMX_PTR        configData)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    if (m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Set Config in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    if ( m_state == OMX_StateExecuting)
    {
        DEBUG_PRINT_ERROR("set_config:Ignore in Exe state\n");
        return OMX_ErrorInvalidState;
    }

    switch (configIndex)
    {
    case OMX_IndexConfigAudioVolume:
        {
            OMX_AUDIO_CONFIG_VOLUMETYPE *vol = (OMX_AUDIO_CONFIG_VOLUMETYPE*)
                configData;
            if (vol->nPortIndex == OMX_CORE_INPUT_PORT_INDEX)
            {
                if ((vol->sVolume.nValue <= OMX_AENC_MAX) &&
                    (vol->sVolume.nValue >= OMX_AENC_MIN))
                {
                    m_volume = vol->sVolume.nValue;
                    if (BITMASK_ABSENT(&m_flags, OMX_COMPONENT_MUTED))
                    {
                        /* ioctl(m_drv_fd, AUDIO_VOLUME,
                        m_volume * OMX_ADEC_VOLUME_STEP); */
                    }

                } else
                {
                    eRet = OMX_ErrorBadParameter;
                }
            } else
            {
                eRet = OMX_ErrorBadPortIndex;
            }
        }
        break;

    case OMX_IndexConfigAudioMute:
        {
            OMX_AUDIO_CONFIG_MUTETYPE *mute = (OMX_AUDIO_CONFIG_MUTETYPE*)
                configData;
            if (mute->nPortIndex == OMX_CORE_INPUT_PORT_INDEX)
            {
                if (mute->bMute == OMX_TRUE)
                {
                    BITMASK_SET(&m_flags, OMX_COMPONENT_MUTED);
                    /* ioctl(m_drv_fd, AUDIO_VOLUME, 0); */
                } else
                {
                    BITMASK_CLEAR(&m_flags, OMX_COMPONENT_MUTED);
                    /* ioctl(m_drv_fd, AUDIO_VOLUME,
                    m_volume * OMX_ADEC_VOLUME_STEP); */
                }
            } else
            {
                eRet = OMX_ErrorBadPortIndex;
            }
        }
        break;

    default:
        eRet = OMX_ErrorUnsupportedIndex;
        break;
    }
    return eRet;
}

/* ======================================================================
FUNCTION
  omx_aac_aenc::GetExtensionIndex

DESCRIPTION
  OMX GetExtensionIndex method implementaion.  <TBD>

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if everything successful.

========================================================================== */
OMX_ERRORTYPE  omx_aac_aenc::get_extension_index(OMX_IN OMX_HANDLETYPE      hComp,
                                                  OMX_IN OMX_STRING      paramName,
                                                  OMX_OUT OMX_INDEXTYPE* indexType)
{
    if (m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Get Extension Index in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    DEBUG_PRINT_ERROR("Error, Not implemented\n");
    return OMX_ErrorNotImplemented;
}

/* ======================================================================
FUNCTION
  omx_aac_aenc::GetState

DESCRIPTION
  Returns the state information back to the caller.<TBD>

PARAMETERS
  <TBD>.

RETURN VALUE
  Error None if everything is successful.
========================================================================== */
OMX_ERRORTYPE  omx_aac_aenc::get_state(OMX_IN OMX_HANDLETYPE  hComp,
                                        OMX_OUT OMX_STATETYPE* state)
{
    *state = m_state;
    DEBUG_PRINT("Returning the state %d\n",*state);
    return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
  omx_aac_aenc::ComponentTunnelRequest

DESCRIPTION
  OMX Component Tunnel Request method implementation. <TBD>

PARAMETERS
  None.

RETURN VALUE
  OMX Error None if everything successful.

========================================================================== */
OMX_ERRORTYPE  omx_aac_aenc::component_tunnel_request
(
    OMX_IN OMX_HANDLETYPE                hComp,
    OMX_IN OMX_U32                        port,
    OMX_IN OMX_HANDLETYPE        peerComponent,
    OMX_IN OMX_U32                    peerPort,
    OMX_INOUT OMX_TUNNELSETUPTYPE* tunnelSetup)
{
    DEBUG_PRINT_ERROR("Error: component_tunnel_request Not Implemented\n");
    return OMX_ErrorNotImplemented;
}

/* ======================================================================
FUNCTION
  omx_aac_aenc::AllocateInputBuffer

DESCRIPTION
  Helper function for allocate buffer in the input pin

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_aac_aenc::allocate_input_buffer
(
    OMX_IN OMX_HANDLETYPE                hComp,
    OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
    OMX_IN OMX_U32                        port,
    OMX_IN OMX_PTR                     appData,
    OMX_IN OMX_U32                       bytes)
{
    OMX_ERRORTYPE         eRet = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE  *bufHdr;
    unsigned              nBufSize = MAX(bytes, input_buffer_size);
    char                  *buf_ptr;

    buf_ptr = (char *) calloc( (nBufSize + sizeof(OMX_BUFFERHEADERTYPE) ) , 1);

    if (buf_ptr != NULL)
    {
        bufHdr = (OMX_BUFFERHEADERTYPE *) buf_ptr;
        *bufferHdr = bufHdr;
        memset(bufHdr,0,sizeof(OMX_BUFFERHEADERTYPE));

        bufHdr->pBuffer           = (OMX_U8 *)((buf_ptr) +
            sizeof(OMX_BUFFERHEADERTYPE));
        bufHdr->nSize             = sizeof(OMX_BUFFERHEADERTYPE);
        bufHdr->nVersion.nVersion = OMX_SPEC_VERSION;
        bufHdr->nAllocLen         = nBufSize;
        bufHdr->pAppPrivate       = appData;
        bufHdr->nInputPortIndex   = OMX_CORE_INPUT_PORT_INDEX;
        m_input_buf_hdrs.insert(bufHdr, NULL);

        m_inp_current_buf_count++;
        DEBUG_PRINT("AIB:bufHdr %x bufHdr->pBuffer %x m_inp_buf_cnt=%d bytes=%d", \
            bufHdr, bufHdr->pBuffer,m_inp_current_buf_count,
            bytes);

    } else
    {
        DEBUG_PRINT("Input buffer memory allocation failed 1 \n");
        eRet =  OMX_ErrorInsufficientResources;
    }
    return eRet;
}

OMX_ERRORTYPE  omx_aac_aenc::allocate_output_buffer
(
    OMX_IN OMX_HANDLETYPE                hComp,
    OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
    OMX_IN OMX_U32                        port,
    OMX_IN OMX_PTR                     appData,
    OMX_IN OMX_U32                       bytes)
{
    OMX_ERRORTYPE         eRet = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE  *bufHdr;
    unsigned                   nBufSize = MAX(bytes,output_buffer_size);
    char                  *buf_ptr;

    if (m_out_current_buf_count < m_out_act_buf_count)
    {
        buf_ptr = (char *) calloc( (nBufSize + sizeof(OMX_BUFFERHEADERTYPE) ) , 1);

        if (buf_ptr != NULL)
        {
            bufHdr = (OMX_BUFFERHEADERTYPE *) buf_ptr;
            *bufferHdr = bufHdr;
            memset(bufHdr,0,sizeof(OMX_BUFFERHEADERTYPE));

            bufHdr->pBuffer           = (OMX_U8 *)((buf_ptr) +
                sizeof(OMX_BUFFERHEADERTYPE));
            bufHdr->nSize             = sizeof(OMX_BUFFERHEADERTYPE);
            bufHdr->nVersion.nVersion = OMX_SPEC_VERSION;
            bufHdr->nAllocLen         = nBufSize;
            bufHdr->pAppPrivate       = appData;
            bufHdr->nOutputPortIndex   = OMX_CORE_OUTPUT_PORT_INDEX;
            m_output_buf_hdrs.insert(bufHdr, NULL);
            m_out_current_buf_count++;
            DEBUG_PRINT("AOB::bufHdr %x bufHdr->pBuffer %x m_out_buf_cnt=%d "\
                "bytes=%d",bufHdr, bufHdr->pBuffer,\
                m_out_current_buf_count, bytes);
        } else
        {
            DEBUG_PRINT("Output buffer memory allocation failed 1 \n");
            eRet =  OMX_ErrorInsufficientResources;
        }
    } else
    {
        DEBUG_PRINT("Output buffer memory allocation failed\n");
        eRet =  OMX_ErrorInsufficientResources;
    }
    return eRet;
}


// AllocateBuffer  -- API Call
/* ======================================================================
FUNCTION
  omx_aac_aenc::AllocateBuffer

DESCRIPTION
  Returns zero if all the buffers released..

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_aac_aenc::allocate_buffer
(
    OMX_IN OMX_HANDLETYPE                hComp,
    OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
    OMX_IN OMX_U32                        port,
    OMX_IN OMX_PTR                     appData,
    OMX_IN OMX_U32                       bytes)
{

    OMX_ERRORTYPE eRet = OMX_ErrorNone;

    if (m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Allocate Buf in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    // What if the client calls again.
    if (OMX_CORE_INPUT_PORT_INDEX == port)
    {
        eRet = allocate_input_buffer(hComp,bufferHdr,port,appData,bytes);
    } else if (OMX_CORE_OUTPUT_PORT_INDEX == port)
    {
        eRet = allocate_output_buffer(hComp,bufferHdr,port,appData,bytes);
    } else
    {
        DEBUG_PRINT_ERROR("Error: Invalid Port Index received %d\n",
            (int)port);
        eRet = OMX_ErrorBadPortIndex;
    }

    if (eRet == OMX_ErrorNone)
    {
        DEBUG_PRINT("allocate_buffer:  before allocate_done \n");
        if (allocate_done())
        {
            DEBUG_PRINT("allocate_buffer:  after allocate_done \n");
            m_is_alloc_buf++;
            if (BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING))
            {
                BITMASK_CLEAR(&m_flags, OMX_COMPONENT_IDLE_PENDING);
                post_command(OMX_CommandStateSet,OMX_StateIdle,
                    OMX_COMPONENT_GENERATE_EVENT);
                DEBUG_PRINT("allocate_buffer:  post idle transition event \n");
            }
            DEBUG_PRINT("allocate_buffer:  complete \n");
        }
        if (port == OMX_CORE_INPUT_PORT_INDEX && m_inp_bPopulated)
        {
            if (BITMASK_PRESENT(&m_flags,OMX_COMPONENT_INPUT_ENABLE_PENDING))
            {
                BITMASK_CLEAR((&m_flags),OMX_COMPONENT_INPUT_ENABLE_PENDING);
                post_command(OMX_CommandPortEnable, OMX_CORE_INPUT_PORT_INDEX,
                    OMX_COMPONENT_GENERATE_EVENT);
            }
        }
        if (port == OMX_CORE_OUTPUT_PORT_INDEX && m_out_bPopulated)
        {
            if (BITMASK_PRESENT(&m_flags,OMX_COMPONENT_OUTPUT_ENABLE_PENDING))
            {
                BITMASK_CLEAR((&m_flags),OMX_COMPONENT_OUTPUT_ENABLE_PENDING);
                DEBUG_PRINT("AllocBuf-->is_out_th_sleep=%d\n",is_out_th_sleep);
                pthread_mutex_lock(&m_out_th_lock_1);
                if (is_out_th_sleep)
                {
                    is_out_th_sleep = false;
                    DEBUG_DETAIL("AllocBuf:WAKING UP OUT THREADS\n");
                    out_th_wakeup();
                }
                pthread_mutex_unlock(&m_out_th_lock_1);
                post_command(OMX_CommandPortEnable, OMX_CORE_OUTPUT_PORT_INDEX,
                    OMX_COMPONENT_GENERATE_EVENT);
            }
        }
    }
    DEBUG_PRINT("Allocate Buffer exit with ret Code %d\n", eRet);
    return eRet;
}

/*=============================================================================
FUNCTION:
  use_buffer

DESCRIPTION:
  OMX Use Buffer method implementation.

INPUT/OUTPUT PARAMETERS:
  [INOUT] bufferHdr
  [IN] hComp
  [IN] port
  [IN] appData
  [IN] bytes
  [IN] buffer

RETURN VALUE:
  OMX_ERRORTYPE

Dependency:
  None

SIDE EFFECTS:
  None
=============================================================================*/
OMX_ERRORTYPE  omx_aac_aenc::use_buffer
(
    OMX_IN OMX_HANDLETYPE            hComp,
    OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
    OMX_IN OMX_U32                   port,
    OMX_IN OMX_PTR                   appData,
    OMX_IN OMX_U32                   bytes,
    OMX_IN OMX_U8*                   buffer)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;

    if (OMX_CORE_INPUT_PORT_INDEX == port)
    {
        eRet = use_input_buffer(hComp,bufferHdr,port,appData,bytes,buffer);

    } else if (OMX_CORE_OUTPUT_PORT_INDEX == port)
    {
        eRet = use_output_buffer(hComp,bufferHdr,port,appData,bytes,buffer);
    } else
    {
        DEBUG_PRINT_ERROR("Error: Invalid Port Index received %d\n",(int)port);
        eRet = OMX_ErrorBadPortIndex;
    }

    if (eRet == OMX_ErrorNone)
    {
        DEBUG_PRINT("Checking for Output Allocate buffer Done");
        if (allocate_done())
        {
            if (BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING))
            {
                BITMASK_CLEAR(&m_flags, OMX_COMPONENT_IDLE_PENDING);
                post_command(OMX_CommandStateSet,OMX_StateIdle,
                    OMX_COMPONENT_GENERATE_EVENT);
            }
        }
        if (port == OMX_CORE_INPUT_PORT_INDEX && m_inp_bPopulated)
        {
            if (BITMASK_PRESENT(&m_flags,OMX_COMPONENT_INPUT_ENABLE_PENDING))
            {
                BITMASK_CLEAR((&m_flags),OMX_COMPONENT_INPUT_ENABLE_PENDING);
                post_command(OMX_CommandPortEnable, OMX_CORE_INPUT_PORT_INDEX,
                    OMX_COMPONENT_GENERATE_EVENT);

            }
        }
        if (port == OMX_CORE_OUTPUT_PORT_INDEX && m_out_bPopulated)
        {
            if (BITMASK_PRESENT(&m_flags,OMX_COMPONENT_OUTPUT_ENABLE_PENDING))
            {
                BITMASK_CLEAR((&m_flags),OMX_COMPONENT_OUTPUT_ENABLE_PENDING);
                post_command(OMX_CommandPortEnable, OMX_CORE_OUTPUT_PORT_INDEX,
                    OMX_COMPONENT_GENERATE_EVENT);
                pthread_mutex_lock(&m_out_th_lock_1);
                if (is_out_th_sleep)
                {
                    is_out_th_sleep = false;
                    DEBUG_DETAIL("UseBuf:WAKING UP OUT THREADS\n");
                    out_th_wakeup();
                }
                pthread_mutex_unlock(&m_out_th_lock_1);
            }
        }
    }
    DEBUG_PRINT("Use Buffer for port%d\n", port);
    return eRet;
}
/*=============================================================================
FUNCTION:
  use_input_buffer

DESCRIPTION:
  Helper function for Use buffer in the input pin

INPUT/OUTPUT PARAMETERS:
  [INOUT] bufferHdr
  [IN] hComp
  [IN] port
  [IN] appData
  [IN] bytes
  [IN] buffer

RETURN VALUE:
  OMX_ERRORTYPE

Dependency:
  None

SIDE EFFECTS:
  None
=============================================================================*/
OMX_ERRORTYPE  omx_aac_aenc::use_input_buffer
(
 OMX_IN OMX_HANDLETYPE            hComp,
 OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
 OMX_IN OMX_U32                   port,
 OMX_IN OMX_PTR                   appData,
 OMX_IN OMX_U32                   bytes,
 OMX_IN OMX_U8*                   buffer)
{
    OMX_ERRORTYPE         eRet = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE  *bufHdr;
    unsigned              nBufSize = MAX(bytes, input_buffer_size);
    char                  *buf_ptr;

    if (m_inp_current_buf_count < m_inp_act_buf_count)
    {
        buf_ptr = (char *) calloc(sizeof(OMX_BUFFERHEADERTYPE), 1);

        if (buf_ptr != NULL)
        {
            bufHdr = (OMX_BUFFERHEADERTYPE *) buf_ptr;
            *bufferHdr = bufHdr;
            memset(bufHdr,0,sizeof(OMX_BUFFERHEADERTYPE));

            bufHdr->pBuffer           = (OMX_U8 *)(buffer);
            DEBUG_PRINT("use_input_buffer:bufHdr %x bufHdr->pBuffer %x bytes=%d", \
                bufHdr, bufHdr->pBuffer,bytes);
            bufHdr->nSize             = sizeof(OMX_BUFFERHEADERTYPE);
            bufHdr->nVersion.nVersion = OMX_SPEC_VERSION;
            bufHdr->nAllocLen         = nBufSize;
            input_buffer_size         = nBufSize;
            bufHdr->pAppPrivate       = appData;
            bufHdr->nInputPortIndex   = OMX_CORE_INPUT_PORT_INDEX;
            bufHdr->nOffset           = 0;
            m_input_buf_hdrs.insert(bufHdr, NULL);
            m_inp_current_buf_count++;
        } else
        {
            DEBUG_PRINT("Input buffer memory allocation failed 1 \n");
            eRet =  OMX_ErrorInsufficientResources;
        }
    } else
    {
        DEBUG_PRINT("Input buffer memory allocation failed\n");
        eRet =  OMX_ErrorInsufficientResources;
    }
    return eRet;
}

/*=============================================================================
FUNCTION:
  use_output_buffer

DESCRIPTION:
  Helper function for Use buffer in the output pin

INPUT/OUTPUT PARAMETERS:
  [INOUT] bufferHdr
  [IN] hComp
  [IN] port
  [IN] appData
  [IN] bytes
  [IN] buffer

RETURN VALUE:
  OMX_ERRORTYPE

Dependency:
  None

SIDE EFFECTS:
  None
=============================================================================*/
OMX_ERRORTYPE  omx_aac_aenc::use_output_buffer
(
    OMX_IN OMX_HANDLETYPE            hComp,
    OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
    OMX_IN OMX_U32                   port,
    OMX_IN OMX_PTR                   appData,
    OMX_IN OMX_U32                   bytes,
    OMX_IN OMX_U8*                   buffer)
{
    OMX_ERRORTYPE         eRet = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE  *bufHdr;
    unsigned              nBufSize = MAX(bytes,output_buffer_size);
    char                  *buf_ptr;

    if (bytes < output_buffer_size)
    {
        /* return if o\p buffer size provided by client
        is less than min o\p buffer size supported by omx component*/
        return OMX_ErrorInsufficientResources;
    }

    DEBUG_PRINT("Inside omx_aac_aenc::use_output_buffer");
    if (m_out_current_buf_count < m_out_act_buf_count)
    {

        buf_ptr = (char *) calloc(sizeof(OMX_BUFFERHEADERTYPE), 1);

        if (buf_ptr != NULL)
        {
            bufHdr = (OMX_BUFFERHEADERTYPE *) buf_ptr;
            DEBUG_PRINT("BufHdr=%p buffer=%p\n",bufHdr,buffer);
            *bufferHdr = bufHdr;
            memset(bufHdr,0,sizeof(OMX_BUFFERHEADERTYPE));

            bufHdr->pBuffer           = (OMX_U8 *)(buffer);
            DEBUG_PRINT("use_output_buffer:bufHdr %x bufHdr->pBuffer %x len=%d",
                bufHdr, bufHdr->pBuffer,bytes);
            bufHdr->nSize             = sizeof(OMX_BUFFERHEADERTYPE);
            bufHdr->nVersion.nVersion = OMX_SPEC_VERSION;
            bufHdr->nAllocLen         = nBufSize;
            output_buffer_size        = nBufSize;
            bufHdr->pAppPrivate       = appData;
            bufHdr->nOutputPortIndex   = OMX_CORE_OUTPUT_PORT_INDEX;
            bufHdr->nOffset           = 0;
            m_output_buf_hdrs.insert(bufHdr, NULL);
            m_out_current_buf_count++;

        } else
        {
            DEBUG_PRINT("Output buffer memory allocation failed\n");
            eRet =  OMX_ErrorInsufficientResources;
        }
    } else
    {
        DEBUG_PRINT("Output buffer memory allocation failed 2\n");
        eRet =  OMX_ErrorInsufficientResources;
    }
    return eRet;
}
/**
 @brief member function that searches for caller buffer

 @param buffer pointer to buffer header
 @return bool value indicating whether buffer is found
 */
bool omx_aac_aenc::search_input_bufhdr(OMX_BUFFERHEADERTYPE *buffer)
{

    bool eRet = false;
    OMX_BUFFERHEADERTYPE *temp = NULL;

    //access only in IL client context
    temp = m_input_buf_hdrs.find_ele(buffer);
    if (buffer && temp)
    {
        DEBUG_DETAIL("search_input_bufhdr %x \n", buffer);
        eRet = true;
    }
    return eRet;
}

/**
 @brief member function that searches for caller buffer

 @param buffer pointer to buffer header
 @return bool value indicating whether buffer is found
 */
bool omx_aac_aenc::search_output_bufhdr(OMX_BUFFERHEADERTYPE *buffer)
{

    bool eRet = false;
    OMX_BUFFERHEADERTYPE *temp = NULL;

    //access only in IL client context
    temp = m_output_buf_hdrs.find_ele(buffer);
    if (buffer && temp)
    {
        DEBUG_DETAIL("search_output_bufhdr %x \n", buffer);
        eRet = true;
    }
    return eRet;
}

// Free Buffer - API call
/**
  @brief member function that handles free buffer command from IL client

  This function is a block-call function that handles IL client request to
  freeing the buffer

  @param hComp handle to component instance
  @param port id of port which holds the buffer
  @param buffer buffer header
  @return Error status
*/
OMX_ERRORTYPE  omx_aac_aenc::free_buffer(OMX_IN OMX_HANDLETYPE         hComp,
                                          OMX_IN OMX_U32                 port,
                                          OMX_IN OMX_BUFFERHEADERTYPE* buffer)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;

    DEBUG_PRINT("Free_Buffer buf %x\n", buffer);

    if (m_state == OMX_StateIdle &&
        (BITMASK_PRESENT(&m_flags ,OMX_COMPONENT_LOADING_PENDING)))
    {
        DEBUG_PRINT(" free buffer while Component in Loading pending\n");
    } else if ((m_inp_bEnabled == OMX_FALSE && port == OMX_CORE_INPUT_PORT_INDEX)||
        (m_out_bEnabled == OMX_FALSE && port == OMX_CORE_OUTPUT_PORT_INDEX))
    {
        DEBUG_PRINT("Free Buffer while port %d disabled\n", port);
    } else if (m_state == OMX_StateExecuting || m_state == OMX_StatePause)
    {
        DEBUG_PRINT("Invalid state to free buffer,ports need to be disabled:\
                    OMX_ErrorPortUnpopulated\n");
        post_command(OMX_EventError,
            OMX_ErrorPortUnpopulated,
            OMX_COMPONENT_GENERATE_EVENT);

        return eRet;
    } else
    {
        DEBUG_PRINT("free_buffer: Invalid state to free buffer,ports need to be\
                    disabled:OMX_ErrorPortUnpopulated\n");
        post_command(OMX_EventError,
            OMX_ErrorPortUnpopulated,
            OMX_COMPONENT_GENERATE_EVENT);
    }
    if (OMX_CORE_INPUT_PORT_INDEX == port)
    {
        if (m_inp_current_buf_count != 0)
        {
            m_inp_bPopulated = OMX_FALSE;
            if (true == search_input_bufhdr(buffer))
            {
                /* Buffer exist */
                //access only in IL client context
                DEBUG_PRINT("Free_Buf:in_buffer[%p]\n",buffer);
                m_input_buf_hdrs.erase(buffer);
                if (m_is_alloc_buf)
                {
                    free(buffer);
                }
                m_inp_current_buf_count--;
            } else
            {
                DEBUG_PRINT_ERROR("Free_Buf:Error-->free_buffer, \
                                  Invalid Input buffer header\n");
                eRet = OMX_ErrorBadParameter;
            }
        } else
        {
            DEBUG_PRINT_ERROR("Error: free_buffer,Port Index calculation \
                              came out Invalid\n");
            eRet = OMX_ErrorBadPortIndex;
        }
        if (BITMASK_PRESENT((&m_flags),OMX_COMPONENT_INPUT_DISABLE_PENDING)
            && release_done(0))
        {
            DEBUG_PRINT("INPUT PORT MOVING TO DISABLED STATE \n");
            BITMASK_CLEAR((&m_flags),OMX_COMPONENT_INPUT_DISABLE_PENDING);
            post_command(OMX_CommandPortDisable,
                OMX_CORE_INPUT_PORT_INDEX,
                OMX_COMPONENT_GENERATE_EVENT);
        }
    } else if (OMX_CORE_OUTPUT_PORT_INDEX == port)
    {
        if (m_out_current_buf_count != 0)
        {
            m_out_bPopulated = OMX_FALSE;
            if (true == search_output_bufhdr(buffer))
            {
                /* Buffer exist */
                //access only in IL client context
                DEBUG_PRINT("Free_Buf:out_buffer[%p]\n",buffer);
                m_output_buf_hdrs.erase(buffer);
                if (m_is_alloc_buf)
                {
                    free(buffer);
                }
                m_out_current_buf_count--;
            } else
            {
                DEBUG_PRINT("Free_Buf:Error-->free_buffer , \
                            Invalid Output buffer header\n");
                eRet = OMX_ErrorBadParameter;
            }
        } else
        {
            eRet = OMX_ErrorBadPortIndex;
        }

        if (BITMASK_PRESENT((&m_flags),OMX_COMPONENT_OUTPUT_DISABLE_PENDING)
            && release_done(1))
        {
            DEBUG_PRINT("OUTPUT PORT MOVING TO DISABLED STATE \n");
            BITMASK_CLEAR((&m_flags),OMX_COMPONENT_OUTPUT_DISABLE_PENDING);
            post_command(OMX_CommandPortDisable,
                OMX_CORE_OUTPUT_PORT_INDEX,
                OMX_COMPONENT_GENERATE_EVENT);

        }
    } else
    {
        eRet = OMX_ErrorBadPortIndex;
    }
    if ((OMX_ErrorNone == eRet) &&
        (BITMASK_PRESENT(&m_flags ,OMX_COMPONENT_LOADING_PENDING)))
    {
        if (release_done(-1))
        {
            ioctl(m_drv_fd, AUDIO_STOP, 0);
            if (m_drv_fd >= 0)
            {
                close(m_drv_fd);
                m_drv_fd = -1;
            }
            // Send the callback now
            BITMASK_CLEAR((&m_flags),OMX_COMPONENT_LOADING_PENDING);
            post_command(OMX_CommandStateSet,
                OMX_StateLoaded,OMX_COMPONENT_GENERATE_EVENT);
        }
    }
    return eRet;
}


/**
 @brief member function that that handles empty this buffer command

 This function meremly queue up the command and data would be consumed
 in command server thread context

 @param hComp handle to component instance
 @param buffer pointer to buffer header
 @return error status
 */
OMX_ERRORTYPE  omx_aac_aenc::empty_this_buffer(OMX_IN OMX_HANDLETYPE         hComp,
                                                OMX_IN OMX_BUFFERHEADERTYPE* buffer)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    bool bPost = false;

    DEBUG_PRINT("ETB:Buf:%x Len %d TS %d numInBuf=%d\n", \
        buffer, buffer->nFilledLen, buffer->nTimeStamp, (nNumInputBuf));
    if (m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT("Empty this buffer in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    if (!m_inp_bEnabled)
    {
        DEBUG_PRINT("empty_this_buffer OMX_ErrorIncorrectStateOperation "\
            "Port Status %d \n", m_inp_bEnabled);
        return OMX_ErrorIncorrectStateOperation;
    }
    if (buffer->nSize != sizeof(OMX_BUFFERHEADERTYPE))
    {
        DEBUG_PRINT("omx_aac_aenc::etb--> Buffer Size Invalid\n");
        return OMX_ErrorBadParameter;
    }
    if (buffer->nVersion.nVersion != OMX_SPEC_VERSION)
    {
        DEBUG_PRINT("omx_aac_aenc::etb--> OMX Version Invalid\n");
        return OMX_ErrorVersionMismatch;
    }

    if (buffer->nInputPortIndex != OMX_CORE_INPUT_PORT_INDEX)
    {
        return OMX_ErrorBadPortIndex;
    }
    if ((m_state != OMX_StateExecuting) &&
        (m_state != OMX_StatePause))
    {
        DEBUG_PRINT_ERROR("Invalid state\n");
        eRet = OMX_ErrorInvalidState;
    }
    if (OMX_ErrorNone == eRet)
    {
        if (search_input_bufhdr(buffer) == true)
        {
            post_input((unsigned)hComp,
                (unsigned) buffer,OMX_COMPONENT_GENERATE_ETB);
        } else
        {
            DEBUG_PRINT_ERROR("Bad header %x \n", buffer);
            eRet = OMX_ErrorBadParameter;
        }
    }
    pthread_mutex_lock(&in_buf_count_lock);
    nNumInputBuf++;
    pthread_mutex_unlock(&in_buf_count_lock);
    return eRet;
}
/**
  @brief member function that writes data to kernel driver

  @param hComp handle to component instance
  @param buffer pointer to buffer header
  @return error status
 */
OMX_ERRORTYPE  omx_aac_aenc::empty_this_buffer_proxy
(
    OMX_IN OMX_HANDLETYPE         hComp,
    OMX_BUFFERHEADERTYPE* buffer)
{
    write(m_drv_fd, buffer->pBuffer, buffer->nFilledLen);
    post_input((unsigned) & hComp,(unsigned) buffer,
        OMX_COMPONENT_GENERATE_BUFFER_DONE);
    return OMX_ErrorNone;
}

OMX_ERRORTYPE  omx_aac_aenc::fill_this_buffer_proxy
(
    OMX_IN OMX_HANDLETYPE         hComp,
    OMX_BUFFERHEADERTYPE* buffer)
{
    static int count = 0;
    int nDatalen = 0;

    /* Assume fill this buffer function has already checked
    validity of buffer */
    DEBUG_PRINT("Inside fill_this_buffer_proxy \n");


    if(search_output_bufhdr(buffer) == true)
    {
        nDatalen = read(m_drv_fd, buffer->pBuffer,output_buffer_size);
        DEBUG_PRINT("FTBP: read buffer %p  #%d of size = %d\n",buffer->pBuffer,
            ++count, nDatalen);

        if((nDatalen < 0) || (nDatalen > output_buffer_size))
        {
            DEBUG_PRINT("FTBP: data length read0 %d\n",nDatalen);
            buffer->nFilledLen = 0;
            //post_event_output((unsigned) & hComp,(unsigned) buffer,OMX_COMPONENT_GENERATE_FRAME_DONE, true);
            frame_done_cb(buffer);
        }
        else
        {
            buffer->nFilledLen = nDatalen;
            DEBUG_PRINT("FTBP: valid data length read = %d\n",buffer->nFilledLen);
            //post_event_output((unsigned) & hComp,(unsigned) buffer,OMX_COMPONENT_GENERATE_FRAME_DONE, true);
            frame_done_cb(buffer);
        }
        DEBUG_PRINT("FTBP-->nNumOutputBuf=%d m_idle_transition=%d\n",nNumOutputBuf,m_idle_transition);
        if(!nNumOutputBuf && m_idle_transition )
        {
            event_complete();
        }
    }
    else
    {
        DEBUG_PRINT("\n Invalid buffer in FTB \n");
    }

    return OMX_ErrorNone;

}

/* ======================================================================
FUNCTION
  omx_aac_aenc::FillThisBuffer

DESCRIPTION
  IL client uses this method to release the frame buffer
  after displaying them.

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
OMX_ERRORTYPE  omx_aac_aenc::fill_this_buffer
(
    OMX_IN OMX_HANDLETYPE         hComp,
    OMX_IN OMX_BUFFERHEADERTYPE* buffer)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    if (buffer->nSize != sizeof(OMX_BUFFERHEADERTYPE))
    {
        DEBUG_PRINT("omx_aac_aenc::ftb--> Buffer Size Invalid\n");
        return OMX_ErrorBadParameter;
    }
    if (m_out_bEnabled == OMX_FALSE)
    {
        return OMX_ErrorIncorrectStateOperation;
    }

    if (buffer->nVersion.nVersion != OMX_SPEC_VERSION)
    {
        DEBUG_PRINT("omx_aac_aenc::ftb--> OMX Version Invalid\n");
        return OMX_ErrorVersionMismatch;
    }
    if (buffer->nOutputPortIndex != OMX_CORE_OUTPUT_PORT_INDEX)
    {
        return OMX_ErrorBadPortIndex;
    }
    pthread_mutex_lock(&out_buf_count_lock);
    nNumOutputBuf++;
    DEBUG_DETAIL("FTB:nNumOutputBuf is %d", nNumOutputBuf);
    pthread_mutex_unlock(&out_buf_count_lock);
    post_output((unsigned)hComp,
        (unsigned) buffer,OMX_COMPONENT_GENERATE_FTB);
    return eRet;
}

/* ======================================================================
FUNCTION
  omx_aac_aenc::SetCallbacks

DESCRIPTION
  Set the callbacks.

PARAMETERS
  None.

RETURN VALUE
  OMX Error None if everything successful.

========================================================================== */
OMX_ERRORTYPE  omx_aac_aenc::set_callbacks(OMX_IN OMX_HANDLETYPE        hComp,
                                            OMX_IN OMX_CALLBACKTYPE* callbacks,
                                            OMX_IN OMX_PTR             appData)
{
    m_cb       = *callbacks;
    m_app_data =    appData;

    return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
  omx_aac_aenc::ComponentDeInit

DESCRIPTION
  Destroys the component and release memory allocated to the heap.

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if everything successful.

========================================================================== */
OMX_ERRORTYPE  omx_aac_aenc::component_deinit(OMX_IN OMX_HANDLETYPE hComp)
{
    if (OMX_StateLoaded != m_state && OMX_StateInvalid != m_state)
    {
        DEBUG_PRINT_ERROR("Warning: Received DeInit when not in "\
            "LOADED state, cur_state %d\n",m_state);
    }
    nNumInputBuf = 0;
    nNumOutputBuf = 0;
    m_is_alloc_buf = 0;
    m_out_act_buf_count = 0;
    m_inp_act_buf_count = 0;
    pthread_mutex_lock(&m_in_th_lock_1);
    if (is_in_th_sleep)
    {
        is_in_th_sleep = false;
        DEBUG_DETAIL("PE:WAKING UP IN THREADS\n");
        in_th_wakeup();
    }
    pthread_mutex_unlock(&m_in_th_lock_1);
    pthread_mutex_lock(&m_out_th_lock_1);
    if (is_out_th_sleep)
    {
        is_out_th_sleep = false;
        DEBUG_DETAIL("SCP:WAKING UP OUT THREADS\n");
        out_th_wakeup();
    }
    pthread_mutex_unlock(&m_out_th_lock_1);
    if (m_ipc_to_in_th != NULL)
    {
        omx_aac_thread_stop(m_ipc_to_in_th);
        m_ipc_to_in_th = NULL;
    }

    if (pcm_input ==1)
    {
        if (m_ipc_to_cmd_th != NULL)
        {
            omx_aac_thread_stop(m_ipc_to_cmd_th);
            m_ipc_to_cmd_th = NULL;
        }
    }
    if (m_ipc_to_out_th != NULL)
    {
        omx_aac_thread_stop(m_ipc_to_out_th);
        m_ipc_to_out_th = NULL;
    }

    m_inp_current_buf_count=0;
    m_out_current_buf_count=0;
    m_inp_bEnabled = OMX_TRUE;
    m_out_bEnabled = OMX_TRUE;
    m_inp_bPopulated = OMX_FALSE;
    m_out_bPopulated = OMX_FALSE;
    m_idle_transition = 0;
    if (m_drv_fd >= 0)
    {
        close(m_drv_fd);
    } else
    {
        DEBUG_PRINT_ERROR(" aac device already closed \n");
    }

    m_comp_deinit=1;
    m_is_out_th_sleep = 1;
    m_is_in_th_sleep = 1;

    DEBUG_PRINT("************************************\n");
    DEBUG_PRINT(" DEINIT COMPLETED");
    DEBUG_PRINT("************************************\n");
    return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
  omx_aac_aenc::UseEGLImage

DESCRIPTION
  OMX Use EGL Image method implementation <TBD>.

PARAMETERS
  <TBD>.

RETURN VALUE
  Not Implemented error.

========================================================================== */
OMX_ERRORTYPE  omx_aac_aenc::use_EGL_image
(
    OMX_IN OMX_HANDLETYPE                hComp,
    OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
    OMX_IN OMX_U32                        port,
    OMX_IN OMX_PTR                     appData,
    OMX_IN void*                      eglImage)
{
    DEBUG_PRINT_ERROR("Error : use_EGL_image:  Not Implemented \n");
    return OMX_ErrorNotImplemented;
}

/* ======================================================================
FUNCTION
  omx_aac_aenc::ComponentRoleEnum

DESCRIPTION
  OMX Component Role Enum method implementation.

PARAMETERS
  <TBD>.

RETURN VALUE
  OMX Error None if everything is successful.
========================================================================== */
OMX_ERRORTYPE  omx_aac_aenc::component_role_enum(OMX_IN OMX_HANDLETYPE hComp,
                                                  OMX_OUT OMX_U8*        role,
                                                  OMX_IN OMX_U32        index)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    const char cmp_role [] = "audio_encoder.aac";

    if(index == 0 && role)
    {
        strncpy((char *)role, cmp_role, sizeof(cmp_role));
    }
    else
    {
        eRet = OMX_ErrorNoMore;
    }
    return eRet;
}

/* ======================================================================
FUNCTION
  omx_aac_aenc::AllocateDone

DESCRIPTION
  Checks if entire buffer pool is allocated by IL Client or not.
  Need this to move to IDLE state.

PARAMETERS
  None.

RETURN VALUE
  true/false.

========================================================================== */
bool omx_aac_aenc::allocate_done(void)
{
    OMX_BOOL bRet = OMX_FALSE;
    if (pcm_input ==1)
    {
        if ((m_inp_act_buf_count == m_inp_current_buf_count)
            &&(m_out_act_buf_count == m_out_current_buf_count))
        {
            bRet=OMX_TRUE;

        }
        if ((m_inp_act_buf_count == m_inp_current_buf_count) && m_inp_bEnabled )
        {
            m_inp_bPopulated = OMX_TRUE;
        }

        if ((m_out_act_buf_count == m_out_current_buf_count) && m_out_bEnabled )
        {
            m_out_bPopulated = OMX_TRUE;
        }
    } else if (pcm_input == 0)
    {
        if (m_out_act_buf_count == m_out_current_buf_count)
        {
            bRet=OMX_TRUE;

        }
        if ((m_out_act_buf_count == m_out_current_buf_count) && m_out_bEnabled )
        {
            m_out_bPopulated = OMX_TRUE;
        }

    }

    return bRet;
}


/* ======================================================================
FUNCTION
  omx_aac_aenc::ReleaseDone

DESCRIPTION
  Checks if IL client has released all the buffers.

PARAMETERS
  None.

RETURN VALUE
  true/false

========================================================================== */
bool omx_aac_aenc::release_done(OMX_U32 param1)
{
    DEBUG_PRINT("Inside omx_aac_aenc::release_done");

    OMX_BOOL bRet = OMX_FALSE;
    if (param1 == OMX_ALL)
    {
        if ((0 == m_inp_current_buf_count)&&(0 == m_out_current_buf_count))
        {
            bRet=OMX_TRUE;
        }
    } else if (param1 == OMX_CORE_INPUT_PORT_INDEX )
    {
        if ((0 == m_inp_current_buf_count))
        {
            bRet=OMX_TRUE;
        }
    } else if (param1 == OMX_CORE_OUTPUT_PORT_INDEX)
    {
        if ((0 == m_out_current_buf_count))
        {
            bRet=OMX_TRUE;
        }
    }
    return bRet;
}
