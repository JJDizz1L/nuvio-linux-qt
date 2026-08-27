#include "nuvio/ui/NavigationModel.h"

namespace nuvio::ui {

NavigationModel::NavigationModel(QObject* parent) : QObject(parent) {}

void NavigationModel::push(const QString& route)
{
    if (route.isEmpty() || route == m_stack.last()) return;
    m_stack.append(route);
    emit stateChanged();
}

bool NavigationModel::pushIfDifferent(const QString& route)
{
    if (route.isEmpty()) return false;
    if (route == m_stack.last()) return false;
    push(route);
    return true;
}

void NavigationModel::pop()
{
    if (m_stack.size() <= 1) return;
    m_stack.removeLast();
    emit stateChanged();
}

void NavigationModel::popToRoot()
{
    if (m_stack.size() <= 1) return;
    while (m_stack.size() > 1) m_stack.removeLast();
    emit stateChanged();
}

void NavigationModel::replaceTop(const QString& route)
{
    if (route.isEmpty() || route == m_stack.last()) return;
    m_stack.last() = route;
    emit stateChanged();
}

QString NavigationModel::currentRoute() const { return m_stack.last(); }

int NavigationModel::depth() const { return m_stack.size(); }

bool NavigationModel::canPop() const { return m_stack.size() > 1; }

} // namespace nuvio::ui