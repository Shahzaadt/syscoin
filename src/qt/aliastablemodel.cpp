#include "aliastablemodel.h"

#include "guiutil.h"
#include "walletmodel.h"

#include "wallet.h"
#include "base58.h"

#include <QFont>

const QString AliasTableModel::Alias = "A";
const QString AliasTableModel::DataAlias = "D";

struct AliasTableEntry
{
    enum Type {
        Alias,
        DataAlias
    };

    Type type;
    QString value;
    QString alias;
    QString expirationdepth;

    AliasTableEntry() {}
    AliasTableEntry(Type type, const QString &value, const QString &alias, const QString &expirationdepth):
        type(type), value(value), alias(alias), expirationdepth(expirationdepth) {}
};

struct AliasTableEntryLessThan
{
    bool operator()(const AliasTableEntry &a, const AliasTableEntry &b) const
    {
        return a.alias < b.alias;
    }
    bool operator()(const AliasTableEntry &a, const QString &b) const
    {
        return a.alias < b;
    }
    bool operator()(const QString &a, const AliasTableEntry &b) const
    {
        return a < b.alias;
    }
};

// Private implementation
class AliasTablePriv
{
public:
    CWallet *wallet;
    QList<AliasTableEntry> cachedAliasTable;
    AliasTableModel *parent;

    AliasTablePriv(CWallet *wallet, AliasTableModel *parent):
        wallet(wallet), parent(parent) {}

    void refreshAliasTable()
    {
        cachedAliasTable.clear();
        {
            LOCK(wallet->cs_wallet);
//            BOOST_FOREACH(const PAIRTYPE(CTxDestination, std::string)& item, wallet->mapAliasBook)
//            {
//                const CBitcoinAddress& alias = item.first;
//                const std::string& strName = item.second;
//                bool fMine = IsMine(*wallet, alias.Get());
//                cachedAliasTable.append(AliasTableEntry(fMine ? AliasTableEntry::Receiving : AliasTableEntry::Sending,
//                                  QString::fromStdString(strName),
//                                  QString::fromStdString(alias.ToString())));
//            }
        }
        // qLowerBound() and qUpperBound() require our cachedAliasTable list to be sorted in asc order
        qSort(cachedAliasTable.begin(), cachedAliasTable.end(), AliasTableEntryLessThan());
    }

    void updateEntry(const QString &alias, const QString &value, bool isData, int status)
    {
        // Find alias / value in model
        QList<AliasTableEntry>::iterator lower = qLowerBound(
            cachedAliasTable.begin(), cachedAliasTable.end(), alias, AliasTableEntryLessThan());
        QList<AliasTableEntry>::iterator upper = qUpperBound(
            cachedAliasTable.begin(), cachedAliasTable.end(), alias, AliasTableEntryLessThan());
        int lowerIndex = (lower - cachedAliasTable.begin());
        int upperIndex = (upper - cachedAliasTable.begin());
        bool inModel = (lower != upper);
        AliasTableEntry::Type newEntryType = isData ? AliasTableEntry::DataAlias : AliasTableEntry::Alias;

        switch(status)
        {
        case CT_NEW:
            if(inModel)
            {
                OutputDebugStringF("Warning: AliasTablePriv::updateEntry: Got CT_NOW, but entry is already in model\n");
                break;
            }
            parent->beginInsertRows(QModelIndex(), lowerIndex, lowerIndex);
            cachedAliasTable.insert(lowerIndex, AliasTableEntry(newEntryType, value, alias, QString("100")));
            parent->endInsertRows();
            break;
        case CT_UPDATED:
            if(!inModel)
            {
                OutputDebugStringF("Warning: AliasTablePriv::updateEntry: Got CT_UPDATED, but entry is not in model\n");
                break;
            }
            lower->type = newEntryType;
            lower->value = value;
            parent->emitDataChanged(lowerIndex);
            break;
        case CT_DELETED:
            if(!inModel)
            {
                OutputDebugStringF("Warning: AliasTablePriv::updateEntry: Got CT_DELETED, but entry is not in model\n");
                break;
            }
            parent->beginRemoveRows(QModelIndex(), lowerIndex, upperIndex-1);
            cachedAliasTable.erase(lower, upper);
            parent->endRemoveRows();
            break;
        }
    }

    int size()
    {
        return cachedAliasTable.size();
    }

    AliasTableEntry *index(int idx)
    {
        if(idx >= 0 && idx < cachedAliasTable.size())
        {
            return &cachedAliasTable[idx];
        }
        else
        {
            return 0;
        }
    }
};

AliasTableModel::AliasTableModel(CWallet *wallet, WalletModel *parent) :
    QAbstractTableModel(parent),walletModel(parent),wallet(wallet),priv(0)
{
    columns << tr("Alias") << tr("Value")<< tr("ExpirationHeight");
    priv = new AliasTablePriv(wallet, this);
    priv->refreshAliasTable();
}

AliasTableModel::~AliasTableModel()
{
    delete priv;
}

int AliasTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return priv->size();
}

int AliasTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QVariant AliasTableModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();

    AliasTableEntry *rec = static_cast<AliasTableEntry*>(index.internalPointer());

    if(role == Qt::DisplayRole || role == Qt::EditRole)
    {
        switch(index.column())
        {
        case Value:
            if(rec->value.isEmpty() && role == Qt::DisplayRole)
            {
                return tr("(no value)");
            }
            else
            {
                return rec->value;
            }
        case Name:
            return rec->alias;
        }
    }
    else if (role == Qt::FontRole)
    {
        QFont font;
        if(index.column() == Name)
        {
            font = GUIUtil::bitcoinAddressFont();
        }
        return font;
    }
    else if (role == TypeRole)
    {
        switch(rec->type)
        {
        case AliasTableEntry::Alias:
            return Alias;
        case AliasTableEntry::DataAlias:
            return DataAlias;
        default: break;
        }
    }
    return QVariant();
}

bool AliasTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if(!index.isValid())
        return false;
    AliasTableEntry *rec = static_cast<AliasTableEntry*>(index.internalPointer());

    editStatus = OK;

    if(role == Qt::EditRole)
    {
        switch(index.column())
        {
        case Value:
            // Do nothing, if old value == new value
            if(rec->value == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }
            //wallet->SetAliasBookName(CBitcoinAlias(rec->alias.toStdString()).Get(), value.toString().toStdString());
            break;
        case Name:
            // Do nothing, if old alias == new alias
            if(rec->alias == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }
            // Refuse to set invalid alias, set error status and return false
            else if(false /* validate alias */)
            {
                editStatus = INVALID_ALIAS;
                return false;
            }
            // Check for duplicate aliases to prevent accidental deletion of aliases, if you try
            // to paste an existing alias over another alias (with a different label)
            else if(false /* check duplicates */)
            {
                editStatus = DUPLICATE_ALIAS;
                return false;
            }
            // Double-check that we're not overwriting a receiving alias
            else if(rec->type == AliasTableEntry::Alias)
            {
                {
                    // update alias
                }
            }
            break;
        }
        return true;
    }
    return false;
}

QVariant AliasTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation == Qt::Horizontal)
    {
        if(role == Qt::DisplayRole)
        {
            return columns[section];
        }
    }
    return QVariant();
}

Qt::ItemFlags AliasTableModel::flags(const QModelIndex &index) const
{
    if(!index.isValid())
        return 0;
    //AliasTableEntry *rec = static_cast<AliasTableEntry*>(index.internalPointer());

    Qt::ItemFlags retval = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    // only value is editable.
    if(index.column()==Value)
    {
        retval |= Qt::ItemIsEditable;
    }
    return retval;
}

QModelIndex AliasTableModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    AliasTableEntry *data = priv->index(row);
    if(data)
    {
        return createIndex(row, column, priv->index(row));
    }
    else
    {
        return QModelIndex();
    }
}

void AliasTableModel::updateEntry(const QString &alias, const QString &value, bool isMine, int status)
{
    // Update alias book model from Bitcoin core
    priv->updateEntry(alias, value, isMine, status);
}

QString AliasTableModel::addRow(const QString &type, const QString &value, const QString &alias)
{
    std::string strValue = value.toStdString();
    std::string strAlias = alias.toStdString();

    editStatus = OK;

    if(false /*validate alias*/)
    {
        editStatus = INVALID_ALIAS;
        return QString();
    }
    // Check for duplicate aliases
    {
        LOCK(wallet->cs_wallet);
        if(false/* check duplicate aliases */)
        {
            editStatus = DUPLICATE_ALIAS;
            return QString();
        }
    }

    // Add entry

    return QString::fromStdString(strAlias);
}

bool AliasTableModel::removeRows(int row, int count, const QModelIndex &parent)
{
    // refuse to remove aliases.
    return false;
}

/* Look up value for alias, if not found return empty string.
 */
QString AliasTableModel::valueForAlias(const QString &alias) const
{
    return QString::fromStdString("{}");
}

int AliasTableModel::lookupAlias(const QString &alias) const
{
    QModelIndexList lst = match(index(0, Name, QModelIndex()),
                                Qt::EditRole, alias, 1, Qt::MatchExactly);
    if(lst.isEmpty())
    {
        return -1;
    }
    else
    {
        return lst.at(0).row();
    }
}

void AliasTableModel::emitDataChanged(int idx)
{
    emit dataChanged(index(idx, 0, QModelIndex()), index(idx, columns.length()-1, QModelIndex()));
}