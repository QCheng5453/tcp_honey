/*
	TCP Honey Congestion Control
*/

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>
#include <net/tcp.h>

struct honey{
	bool	honey_en;
	bool	if_congested;
	u32		rtt_min;
	u32		rtt;		
};

#define TCP_HONEY_INIT_RTT 1000000 /* 1 sec */

static void tcp_honey_init(struct sock *sk)
{
	struct honey *honey = inet_csk_ca(sk);

	honey->honey_en = true;
	honey->if_congested = false;
	honey->rtt_min = TCP_HONEY_INIT_RTT;
	honey->rtt = TCP_HONEY_INIT_RTT;
}

static void tcp_honey_pkts_acked(struct sock *sk, u32 cnt, s32 rtt_us)
{
	struct honey *honey = inet_csk_ca(sk);

	if (rtt_us > 0)
		honey->rtt = rtt_us;
	honey->rtt_min = min(honey->rtt_min, honey->rtt);
}

static void tcp_honey_state(struct sock *sk, u8 ca_state)
{
	struct honey *honey = inet_csk_ca(sk);

	honey->honey_en = (ca_state == TCP_CA_Open);
}

/* estimate if this is a congestion loss */
static void tcp_honey_congestion_det(struct honey *honey)
{
	honey->if_congested = (honey->rtt > honey->rtt_min << 1U);
}

static void tcp_honey_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct honey *honey = inet_csk_ca(sk);
	u32 inc;

	if (!honey->honey_en) {
		tcp_reno_cong_avoid(sk, ack, acked);
		return;
	}

	tcp_honey_congestion_det(honey);
	/* limited by applications */
/*	if (!tcp_is_cwnd_limited(sk))
		return;
*/
	if (tcp_in_slow_start(tp)){
		/* in slow start, double send window*/
		inc = tp->snd_cwnd;
	} else {
		/* congestion avoidance */
		inc = 1;
	}

	if ((tp->snd_cwnd >> 1U) + (inc >> 1U) > (tp->snd_cwnd_clamp >> 1U)){
		tp->snd_cwnd = (tp->snd_cwnd >> 1U) + (tp->snd_cwnd_clamp >> 1u);
	} else {
		tp->snd_cwnd += inc;
	}
}

static u32 tcp_honey_ssthresh(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct honey *honey = inet_csk_ca(sk);

	if (honey->if_congested)
		return max(tp->snd_cwnd * 4 / 5, 2U);
	else
		return max(tp->snd_cwnd, 2U);
}

static struct tcp_congestion_ops tcp_honey __read_mostly = {
	.init		= tcp_honey_init,
	.ssthresh	= tcp_honey_ssthresh,
	.cong_avoid	= tcp_honey_cong_avoid,
	.pkts_acked	= tcp_honey_pkts_acked,
	.set_state	= tcp_honey_state,

	.owner		= THIS_MODULE,
	.name		= "honey",
};

static int __init tcp_honey_register(void)
{
	BUILD_BUG_ON(sizeof(struct honey) > ICSK_CA_PRIV_SIZE);
	tcp_register_congestion_control(&tcp_honey);
	return 0;
}

static void __exit tcp_honey_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_honey);
}

module_init(tcp_honey_register);
module_exit(tcp_honey_unregister);

MODULE_AUTHOR("Cheng Qian");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP Honey");
