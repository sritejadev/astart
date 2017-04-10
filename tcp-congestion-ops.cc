/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2015 Natale Patriciello <natale.patriciello@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include "tcp-congestion-ops.h"
#include "tcp-socket-base.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpCongestionOps");

NS_OBJECT_ENSURE_REGISTERED (TcpCongestionOps);

TypeId
TcpCongestionOps::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpCongestionOps")
    .SetParent<Object> ()
    .SetGroupName ("Internet")
    
  ;
  return tid;
}

TcpCongestionOps::TcpCongestionOps () : Object ()
{
}

TcpCongestionOps::TcpCongestionOps (const TcpCongestionOps &other) : Object (other)
{
}

TcpCongestionOps::~TcpCongestionOps ()
{
}


// RENO

NS_OBJECT_ENSURE_REGISTERED (TcpNewReno);

TypeId
TcpNewReno::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpNewReno")
    .SetParent<TcpCongestionOps> ()
    .SetGroupName ("Internet")
    .AddConstructor<TcpNewReno> ()
    .AddTraceSource("EREstimate", "The eligible rate estimate",
                    MakeTraceSourceAccessor(&TcpNewReno::m_currentERE),
                    "ns3::TracedValueCallback::Double")
    .AddTraceSource("EstimatedBestRate", "The best allowable sending rate",
                    MakeTraceSourceAccessor(&TcpNewReno::m_bestRate),
                    "ns3::TracedValueCallback::Double")
     .AddTraceSource("EstimatedERE", "The eligible rate estimate",
                    MakeTraceSourceAccessor(&TcpNewReno::m_currentERE),
                    "ns3::TracedValueCallback::Double")
  
  ;
  return tid;
}

TcpNewReno::TcpNewReno (void) : TcpCongestionOps (),
m_currentERE (0),
  m_bestRate (0),
  m_interval (Seconds (0.10)),
  m_lastSampleERE (0),
  m_lastERE (0),
  m_minRtt (Time (0)),
  m_Rtt (Time (0)),
  m_ackedSinceT (0),
  m_lastAck (Time::Min ())
{
  NS_LOG_FUNCTION (this);
}

TcpNewReno::TcpNewReno (const TcpNewReno& sock)
  : TcpCongestionOps (sock),
m_currentERE (sock.m_currentERE),
  m_bestRate (sock.m_bestRate),
  m_lastSampleERE (sock.m_lastSampleERE),
  m_lastERE (sock.m_lastERE),
  m_minRtt (Time (0)),
  m_Rtt (Time (0))
{
  NS_LOG_FUNCTION (this);
}

TcpNewReno::~TcpNewReno (void)
{
}

/**
 * \brief Tcp NewReno slow start algorithm
 *
 * Defined in RFC 5681 as
 *
 * > During slow start, a TCP increments cwnd by at most SMSS bytes for
 * > each ACK received that cumulatively acknowledges new data.  Slow
 * > start ends when cwnd exceeds ssthresh (or, optionally, when it
 * > reaches it, as noted above) or when congestion is observed.  While
 * > traditionally TCP implementations have increased cwnd by precisely
 * > SMSS bytes upon receipt of an ACK covering new data, we RECOMMEND
 * > that TCP implementations increase cwnd, per:
 * >
 * >    cwnd += min (N, SMSS)                      (2)
 * >
 * > where N is the number of previously unacknowledged bytes acknowledged
 * > in the incoming ACK.
 *
 * The ns-3 implementation respect the RFC definition. Linux does something
 * different:
 * \verbatim
u32 tcp_slow_start(struct tcp_sock *tp, u32 acked)
  {
    u32 cwnd = tp->snd_cwnd + acked;

    if (cwnd > tp->snd_ssthresh)
      cwnd = tp->snd_ssthresh + 1;
    acked -= cwnd - tp->snd_cwnd;
    tp->snd_cwnd = min(cwnd, tp->snd_cwnd_clamp);

    return acked;
  }
  \endverbatim
 *
 * As stated, we want to avoid the case when a cumulative ACK increases cWnd more
 * than a segment size, but we keep count of how many segments we have ignored,
 * and return them.
 *
 * \param tcb internal congestion state
 * \param segmentsAcked count of segments acked
 * \return the number of segments not considered for increasing the cWnd
 */
uint32_t
TcpNewReno::SlowStart (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
  NS_LOG_FUNCTION (this << tcb << segmentsAcked);

  if (segmentsAcked >= 1)
    {
      tcb->m_cWnd += tcb->m_segmentSize;
      NS_LOG_INFO ("In SlowStart, updated to cwnd " << tcb->m_cWnd << " ssthresh " << tcb->m_ssThresh);
      return segmentsAcked - 1;
    }

  return 0;
}



/**
 * \brief NewReno congestion avoidance
 *
 * During congestion avoidance, cwnd is incremented by roughly 1 full-sized
 * segment per round-trip time (RTT).
 *
 * \param tcb internal congestion state
 * \param segmentsAcked count of segments acked
 */
void
TcpNewReno::CongestionAvoidance (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
  NS_LOG_FUNCTION (this << tcb << segmentsAcked);

  if (segmentsAcked > 0)
    {
      double adder = static_cast<double> (tcb->m_segmentSize * tcb->m_segmentSize) / tcb->m_cWnd.Get ();
      adder = std::max (1.0, adder);
      tcb->m_cWnd += static_cast<uint32_t> (adder);
      NS_LOG_INFO ("In CongAvoid, updated to cwnd " << tcb->m_cWnd <<
                   " ssthresh " << tcb->m_ssThresh);
    }
}

/**
 * \brief Try to increase the cWnd following the NewReno specification
 *
 * \see SlowStart
 * \see CongestionAvoidance
 *
 * \param tcb internal congestion state
 * \param segmentsAcked count of segments acked
 */
void
TcpNewReno::IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
  NS_LOG_FUNCTION (this << tcb << segmentsAcked);

  /* Follwoing the Adaptive Start algorithm, the m_ssThresh is a maximum of the following three values:
   *         (2 * state->m_segmentSize) this is equivalent to two segments. m_ssThresh should never go below this
   *         (state->m_ssThresh)        the current m_ssThresh value
   *         (m_currentERE * m_minRtt)  this metric is as proposed in the paper
   */
  
  uint32_t substitute = std::max (2 * tcb       ->m_segmentSize, uint32_t (tcb->m_ssThresh));
  tcb->m_ssThresh = std::max (substitute, uint32_t (m_currentERE * static_cast<double> (m_minRtt.GetSeconds ())));

  if (tcb->m_cWnd < tcb->m_ssThresh)
    {
      segmentsAcked = SlowStart (tcb, segmentsAcked);
    }

  if (tcb->m_cWnd >= tcb->m_ssThresh)
    {
      CongestionAvoidance (tcb, segmentsAcked);
    }

  /* At this point, we could have segmentsAcked != 0. This because RFC says
   * that in slow start, we should increase cWnd by min (N, SMSS); if in
   * slow start we receive a cumulative ACK, it counts only for 1 SMSS of
   * increase, wasting the others.
   *
   * // Uncorrect assert, I am sorry
   * NS_ASSERT (segmentsAcked == 0);
   */
}



/**
 * \brief This function is called on receipt of every ACK
 *
 * In this function, we calculate m_minRtt and m_interval, and call the function CalculateERE() which calculates the ERE     
 * (m_currentERE) value
 *
 * \param tcb internal congestion state
 * \param segmentsAcked count of segments acked
 * \param rtt last round-trip-time
 */

void
TcpNewReno::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t packetsAcked,
                        const Time& rtt)
{
   
  
  NS_LOG_FUNCTION (this << tcb << packetsAcked << rtt);

  if (rtt.IsZero ())
    {
      NS_LOG_WARN ("RTT measured is zero!");
      return;
    }
    
 /**
   * Setting the value of m_deltaT. Initially, m_lastAck is set to Time::Min in which case m_lastAck is set to 0 
   * ( because no ACK has been received previously )
   */
  m_lastAck==Time::Min ()? m_deltaT = 0: m_deltaT = Simulator::Now (). GetSeconds () - m_lastAck. GetSeconds ();     
  
  m_lastAck = Simulator::Now ();

  m_ackedSinceT += packetsAcked;
  
  m_Rtt = rtt;
  
  if (m_minRtt.IsZero ())
    {
      m_minRtt = rtt;
    }
  else
    {
      if (rtt < m_minRtt)
        {
          m_minRtt = rtt;
        }
    }

  NS_LOG_LOGIC ("MinRtt: " << m_minRtt.GetMilliSeconds () << "ms");

         m_ereEstimateEvent.Cancel ();
         m_ereEstimateEvent = Simulator::Schedule (rtt, &TcpNewReno::tsetting,
                                                   this, rtt, tcb);
  CalculateBestRate(m_minRtt, tcb);
       
      
  if (0.10 > (rtt.GetSeconds() * (m_bestRate - m_lastERE)) / m_bestRate )
    m_interval = Seconds (0.10);
  else
    m_interval = Seconds((rtt.GetSeconds() * (m_bestRate - m_lastERE)) / m_bestRate);
    
 
  CalculateERE(m_interval,tcb);    

}


/**
 * \brief To calculate the best allowable sending rate
 *
 * This function calculates m_bestRate by using minRtt, which is the smallest time in which an ACK has been received.
 *
 * \param minRtt minimum round-trip-time seen so far
 * \param tcb internal congestion state
 */
void
TcpNewReno::CalculateBestRate (const Time &minRtt, Ptr<TcpSocketState> tcb)
{

  NS_LOG_FUNCTION (this);

  NS_ASSERT (!minRtt.IsZero ());
  
  m_bestRate = double(tcb->m_cWnd / (double ( (minRtt.GetSeconds ()))));

  NS_LOG_LOGIC ("Estimated Best Rate: " << m_bestRate);
}

void
TcpNewReno::tsetting (const Time &rtt, Ptr<TcpSocketState> tcb)
{
        m_ackedSinceT = 0;
 
 }


/**
 * \brief To calculate the Eligible Rate Estimate (ERE), denoted by the variable m_currentERE
 *
 * \param tvalue time interval over which ERE will be calculated
 * \param tcb internal congestion state
 */
void
TcpNewReno::CalculateERE (const Time &tvalue, Ptr<TcpSocketState> tcb)
{
    
  NS_LOG_FUNCTION (this);

  NS_ASSERT (!tvalue.IsZero ());

  m_currentERE = m_ackedSinceT * tcb->m_segmentSize / tvalue.GetSeconds ();
  

  //m_ackedSinceT = 0;

  NS_LOG_LOGIC ("Instantaneous ERE: " << m_currentERE);

  static double u = 0;
  static double umax = 0;
  
  if(m_currentERE > m_lastSampleERE)  
    u = 0.6 * u + 0.4 * (m_currentERE - m_lastSampleERE);
  else
    u = 0.6 * u + 0.4 * (m_lastSampleERE - m_currentERE);
  
  if(u>umax)
    umax = u;
 
  double tk;
  tk = m_Rtt.GetSeconds() + 10*m_Rtt.GetSeconds()*(u/umax);   
  
  // Filter the ERE sample
  double alpha = 0.9;

  alpha = (2*tk - m_deltaT)/(2*tk + m_deltaT);

  double instantaneousERE = m_currentERE;

  m_currentERE = (alpha * m_lastERE) + ((1 - alpha) * instantaneousERE);

  m_lastSampleERE = instantaneousERE;

  m_lastERE = m_currentERE;
   

  NS_LOG_LOGIC ("ERE after filtering: " << m_currentERE);
}


std::string
TcpNewReno::GetName () const
{
  return "TcpNewReno";
}


uint32_t
TcpNewReno::GetSsThresh (Ptr<const TcpSocketState> tcb,
                         uint32_t bytesInFlight)
{
  (void) bytesInFlight;
  NS_LOG_LOGIC ("Current ERE: " << m_currentERE << " minRtt: " <<
                m_minRtt << " ssthresh: " <<
                m_currentERE * static_cast<double> (m_minRtt.GetSeconds ()));
 
  return std::max (2 * tcb->m_segmentSize, uint32_t (m_currentERE * static_cast<double> (m_minRtt.GetSeconds ())));
}



Ptr<TcpCongestionOps>
TcpNewReno::Fork ()
{
  return CopyObject<TcpNewReno> (this);
}

} // namespace ns3
