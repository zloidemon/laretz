/**********************************************************************
 * Copyright 2013 Georg Rudoy <0xd34df00d@gmail.com>
 *
 * Boost Software License - Version 1.0 - August 17th, 2003
 *
 * Permission is hereby granted, free of charge, to any person or organization
 * obtaining a copy of the software and accompanying documentation covered by
 * this license (the "Software") to use, reproduce, display, distribute,
 * execute, and transmit the Software, and to prepare derivative works of the
 * Software, and to permit third-parties to whom the Software is furnished to
 * do so, all subject to the following:
 *
 * The copyright notices in the Software and this entire statement, including
 * the above license grant, this restriction and the following disclaimer,
 * must be included in all copies of the Software, in whole or in part, and
 * all derivative works of the Software, unless such copies or derivative
 * works are solely in the form of machine-executable object code generated by
 * a source language processor.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
 * FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 **********************************************************************/

#include "dboperator.h"
#include "db.h"
#include "operation.h"
#include "dbresult.h"

namespace Laretz
{
	DBOperator::DBOperator (DB_ptr db)
	: m_db { db }
	, m_op2func {
			{ OpType::List, [this] (const Operation& op) { return list (op); } },
			{ OpType::Fetch, [this] (const Operation& op) { return fetch (op); } },
			{ OpType::Append, [this] (const Operation& op) { return append (op); } }
		}
	{
	}

	std::vector<DBResult> DBOperator::operator() (const std::vector<Operation>& ops)
	{
		std::vector<DBResult> result;
		for (const auto& op : ops)
		{
			const auto& opResult = apply (op);
			std::copy (opResult.begin (), opResult.end (), std::back_inserter (result));
		}
		return result;
	}

	std::vector<DBResult> DBOperator::apply (const Operation& op)
	{
		return m_op2func.find (op.getType ())->second (op);
	}

	std::vector<DBResult> DBOperator::list (const Operation& op)
	{
		const auto& reqItem = op.getItem ();
		const auto& items = m_db->enumerateItems (reqItem.getSeq (), reqItem.getParentId ());
		std::vector<ShortItem> shorts;
		shorts.reserve (items.size ());
		for (const auto& item : items)
			shorts.emplace_back (item);
		return { { shorts } };
	}

	std::vector<DBResult> DBOperator::fetch (const Operation& op)
	{
		if (const auto optItem = m_db->loadItem (op.getItem ().getId ()))
			return { { { std::vector<Item> { *optItem } } } };
		else
			return {};
	}

	std::vector<DBResult> DBOperator::append (const Operation& op)
	{
		m_db->addItem (op.getItem ());
		return {};
	}
}
