#include "czlibPCH.h"
#include "crazygaze/Strand.h"

namespace cz
{

Strand::Strand(CompletionPort iocp) : m_iocp(iocp)
{
}

bool Strand::runningInThisThread() const
{
	return Callstack<Strand>::contains(this) != nullptr;
}

void Strand::schedule()
{
	auto op = std::make_unique<StrandOperation>();
	op->owner = this;
	m_iocp.post(std::move(op), 0, 0);
}

void Strand::process()
{
	Callstack<Strand>::Context ctx(this);
	while (true)
	{
		std::function<void()> handler;
		m_data([&](Data &data)
		{
			if (data.q.size())
			{
				handler = std::move(data.q.front());
				data.q.pop();
			}
			else
			{
				data.running = false;
			}
		});

		if (handler)
			handler();
		else
			return;
	}
}

void Strand::StrandOperation::execute(bool aborted, unsigned bytesTransfered, uint64_t completionKey)
{
	CZ_ASSERT(!aborted); // Strand doesn't allow operations to be aborted
	owner->process();
}

}


