// Driver for Soundplane Model A.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __UNPACKER__
#define __UNPACKER__

#include "SoundplaneModelA.h"

/**
 * The Soundplane model A USB protocol exposes two separate endpoints with
 * separate streams, each for one half of the board. A soundplane driver has to
 * take these two streams and unify them into one stream of control values.
 *
 * Unpacker objects do this, so that the SoundplaneDriver objects can focus on
 * the actual USB stuff.
 */
template<int StoredTransfersPerEndpoint, int Endpoints>
class Unpacker
{
	static_assert(Endpoints == 2, "Unpacker only supports 2 endpoints at the moment");

	/**
	 * A basic ring buffer
	 */
	template<typename T, size_t Capacity>
	class RingBuffer
	{
	public:
		RingBuffer() = default;

		RingBuffer(const RingBuffer &) = delete;
		RingBuffer &operator=(const RingBuffer &) = delete;

		/**
		 * If the buffer overflows, the oldest value is silently discarded.
		 */
		void push_back(T value)
		{
			mData[mIdx] = value;
			mSize = std::max(Capacity, mSize + 1);
			mIdx = (mIdx + 1) % Capacity;
		}

		void pop_front()
		{
			mSize--;
		}

		T &front()
		{
			// Add 2 * Capacity to ensure that the number that's %Capacity'd
			// isn't negative.
			return mData[(mIdx + (2 * Capacity) - 1 - mSize) % Capacity];
		}

		bool empty() const
		{
			return mSize == 0;
		}
	private:
		size_t mSize = 0;
		size_t mIdx = 0;
		T mData[Capacity];
	};

public:
	/**
	 * Feed the Unpacker with a number of packets. The Unpacker tolerates packet
	 * losses, but it does not tolerate packet reordering. If a packet arrives
	 * too early, all subsequent packets with a lower sequence number are
	 * dropped.
	 *
	 * Caution: The Unpacker saves the packets pointer. It is expected to stay
	 * valid for as long as the object is alive, or until
	 * StoredTransfersPerEndpoint subsequent calls to gotTransfer have been
	 * made (by that time, Unpacker will forget it).
	 *
	 * The expected way to deal with this constraint is for the driver to
	 * allocate StoredTransfersPerEndpoint extra transfer buffers, so that
	 * the transfer buffers that the Unpacker works with are never used by the
	 * USB stack.
	 */
	void gotTransfer(int endpoint, SoundplaneADataPacket* packets, int numPackets)
	{
		mTransfers[endpoint].push_back(Transfer(packets, numPackets));

		Transfer* ts[2] = { getOldestTransfer(0), getOldestTransfer(1) };
		while (ts[0] && ts[1])
		{
			SoundplaneADataPacket& p0 = ts[0]->currentPacket();
			SoundplaneADataPacket& p1 = ts[1]->currentPacket();
			if (p0.seqNum == p1.seqNum)
			{
				// The sequence numbers line up
				// FIXME: K1_unpack_float2(p0.packedData, p1.packedData, TODO);
			}
			else
			{
				// The oldest packet we have for one endpoint is older than
				// the oldest for the other. In this case we discard the older
				// packet.
				//
				// FIXME: < is not the right thing to do here. Handle overflow.
				int olderTransferIdx = p0.seqNum < p1.seqNum ? 0 : 1;
				if (ts[olderTransferIdx]->popCurrentPacket())
				{
					mTransfers[olderTransferIdx].pop_front();
					ts[olderTransferIdx] = getOldestTransfer(0);
				}
			}
		}
	}

private:
	struct Transfer
	{
		Transfer() = default;

		Transfer(SoundplaneADataPacket* packets, int numPackets) :
			mPackets(packets),
			mNumPackets(numPackets) {}

		SoundplaneADataPacket& currentPacket()
		{
			return mPackets[mCurrentPacketIndex];
		}

		/**
		 * Returns true if there are no packets left.
		 */
		bool popCurrentPacket()
		{
			mCurrentPacketIndex++;
			return mCurrentPacketIndex == mNumPackets;
		}
	private:
		/**
		 * Index to the first packet that has not yet been processed.
		 */
		int mCurrentPacketIndex = 0;
		SoundplaneADataPacket* mPackets = nullptr;
		int mNumPackets = 0;
	};

	/**
	 * May return nullptr
	 */
	Transfer *getOldestTransfer(int endpoint)
	{
		if (mTransfers[endpoint].empty())
		{
			return nullptr;
		}
		return &mTransfers[endpoint].front();
	}

	RingBuffer<Transfer, StoredTransfersPerEndpoint> mTransfers[Endpoints];
};

#endif // __UNPACKER__
