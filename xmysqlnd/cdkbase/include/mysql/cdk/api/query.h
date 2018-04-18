/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 2006-2018 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
*/

#ifndef CDK_API_QUERY_H
#define CDK_API_QUERY_H

#include "obj_ref.h"
#include "expression.h"
#include "document.h"
#include "../foundation/types.h"

namespace cdk {
namespace api {

using foundation::string;

class String_processor
{
public:
  virtual void val(const string&) = 0;
};

typedef Expr_list< Expr_base<String_processor> >  String_list;


/*
  Classes for representing different parts of a query such as
  LIMIT, ORDER BY etc.
*/

struct Sort_direction
{
  enum value { ASC = 1, DESC = 2 };
};


template <typename row_count_type>
class Limit
{
public:

  typedef row_count_type row_count_t;

  virtual row_count_t get_row_count() const =0;
  virtual const row_count_t* get_offset() const
  { return NULL; }
};


/*
  Order_by specification is a list of items of type Order_expr. Each item
  is an expression over Order_expr_processor which describes a sorting
  key.
*/

template <class EXPR>
class Order_expr_processor
{
public:

  typedef typename EXPR::Processor  Expr_prc;

  /*
    Report expression used as the sort key. This callback should return
    a processor for processing the expression. The dir parameter defines
    the sorting order.
  */

  virtual Expr_prc* sort_key(Sort_direction::value dir) =0;

};

template <class EXPR>
class Order_expr : public Expr_base< Order_expr_processor<EXPR> >
{};

template <class EXPR>
class Order_by
  : public Expr_list< Order_expr<EXPR> >
{
};


/*
  Projection specification is a list of items. Each item is an expression over
  Projection_processor which reports a projection expression and optional alias.
*/

template <class EXPR>
class Projection_processor
{
public:

  typedef typename EXPR::Processor Expr_prc;

  virtual Expr_prc* expr() = 0;
  virtual void      alias(const string&) = 0;
};

template <class EXPR>
class Projection_expr : public Expr_base< Projection_processor<EXPR> >
{};

template <class EXPR>
class Projection
  : public Expr_list< Projection_expr<EXPR> >
{};

struct Lock_mode
{
  enum value { NONE, SHARED, EXCLUSIVE };
};


/*
  View specifications.

  A view specification can be passed to a session method, such as table_find(),
  which sends CRUD query. When view specification is given for a query, then
  this query is saved as a new view, or it replaces the query of an existing
  view.
*/

template <class OPTS>
class View_processor
{
public:

  typedef OPTS  Options;
  typedef String_list::Processor  List_processor;
  enum op_type { CREATE, UPDATE, REPLACE };

  virtual void name(const Table_ref&, op_type type = CREATE) = 0;
  virtual typename Options::Processor* options() = 0;
  virtual List_processor* columns()
  { return NULL; }
};


template <class OPTS>
class View_spec : public Expr_base< View_processor<OPTS> >
{
public:

  typedef OPTS Options;
  typedef typename View_processor<OPTS>::op_type  op_type;
};


/*
  Standard view options: these are aligned with view options defined by
  DevAPI.
*/

struct View_security
{
  enum value {
    DEFINER, INVOKER
  };
};

struct View_algorithm
{
  enum value {
    UNDEFINED,
    MERGE,
    TEMPTABLE
  };
};

struct View_check
{
  enum value {
    LOCAL,
    CASCADED
  };
};


class View_opt_prc
{
public:

  typedef View_security::value  View_security_t;
  typedef View_algorithm::value View_algorithm_t;
  typedef View_check::value     View_check_t;

  virtual void definer(const string&) = 0;
  virtual void security(View_security_t) = 0;
  virtual void algorithm(View_algorithm_t) = 0;
  virtual void check(View_check_t) = 0;
};

typedef Expr_base<View_opt_prc>  View_options;


/*
  Columns specification specifies table columns into which
  table insert operation should insert values. It is a list
  of items, each to be processed with Column_processor to
  describe:

  - name of the table column into which to insert,
  - optional document path if this column holds documents
    - the value will be inserted into specified element
    within the document,
  - optional alias for the column (TODO: how is it used).

  TODO: If alias is not used, consider removing alias()
  from Column_processor.
*/

class Column_processor
{
public:

  typedef cdk::api::string    string;
  typedef Doc_path::Processor Path_prc;

  virtual void name(const string&) =0;
  virtual void alias(const string&) =0;
  virtual Path_prc* path() =0;
};

typedef Expr_list< Expr_base<Column_processor> > Columns;

}}  // cdk::api


namespace cdk {

using api::String_list;

template<>
struct Safe_prc<api::String_processor>
  : Safe_prc_base<api::String_processor>
{
  typedef Safe_prc_base<api::String_processor> Base;
  using Base::Processor;

  Safe_prc(Processor *prc) : Base(prc)
  {}

  Safe_prc(Processor &prc) : Base(&prc)
  {}

  using Base::m_prc;

  void val(const string &val)
  {
    return m_prc ? m_prc->val(val) : (void)NULL;
  }
};


template<>
struct Safe_prc<api::Column_processor>
  : Safe_prc_base<api::Column_processor>
{
  typedef Safe_prc_base<api::Column_processor> Base;
  using Base::Processor;
  typedef Processor::string   string;
  typedef Processor::Path_prc Path_prc;

  Safe_prc(Processor *prc) : Base(prc)
  {}

  Safe_prc(Processor &prc) : Base(&prc)
  {}

  using Base::m_prc;

  void name(const string &n)
  { return m_prc ? m_prc->name(n) : (void)NULL; }

  void alias(const string &a)
  { return m_prc ? m_prc->alias(a) : (void)NULL; }

  Safe_prc<Path_prc> path()
  { return m_prc ? m_prc->path() : NULL; }

};

}  // cdk

#endif
