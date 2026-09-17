'use strict';
const form=document.querySelector('#search-form'),results=document.querySelector('#results'),statusLine=document.querySelector('#status');
const pageSize=10;let offset=0,total=0,requestNumber=0,activeQuery=new URLSearchParams();
function node(tag,text,className){const e=document.createElement(tag);if(text!==undefined)e.textContent=text;if(className)e.className=className;return e;}
function card(book){
 const article=node('article',undefined,'book');article.append(node('div',String(book.format||'файл').toUpperCase(),'book-icon'));
 const body=node('div');body.append(node('h3',book.title||'Без названия'));body.append(node('div','Сообщество: '+(book.community||'не указано'),'metadata'));
 const authors=Array.isArray(book.authors)&&book.authors.length?book.authors.join(', '):'Автор не указан';
 body.append(node('div',`${authors} · ${book.publication_year||'Год не указан'} · ${(Number(book.size_bytes||0)/1048576).toFixed(1)} МиБ`,'metadata'));
 body.append(node('span',book.text_available?'Поиск по тексту доступен':'Текст ещё не извлечён',book.text_available?'badge':'badge pending'));
 for(const tag of book.tags||[])body.append(node('span',tag,'badge'));
 if(book.snippet)body.append(node('p',book.snippet,'snippet'));
 else if(book.annotation){body.append(node('p',book.annotation));if(book.metadata_provenance)body.append(node('div','Описание и тематические метки составлены по содержанию документа.','metadata'));}
 if(/^https:\/\/vk\.com\/wall-?\d+_\d+$/.test(book.source_url||'')){
 const link=node('a','Открыть публикацию VK ↗','source');link.href=book.source_url;link.target='_blank';link.rel='noopener noreferrer';body.append(link);
 }
 article.append(body);return article;
}
async function search(){
 const current=++requestNumber;results.setAttribute('aria-busy','true');statusLine.textContent='Ищем…';document.querySelector('#pagination').hidden=true;results.replaceChildren();
 const query=new URLSearchParams(activeQuery);query.set('limit',pageSize);query.set('offset',offset);
 try{
  const response=await fetch('/api/v1/books?'+query,{signal:AbortSignal.timeout(20000)});
  if(!response.ok)throw new Error(response.status===400?'Проверь значения фильтров.':'Каталог сейчас недоступен. Попробуй ещё раз.');
  const data=await response.json();if(current!==requestNumber)return;total=data.total;
  document.querySelector('#result-title').textContent=`Найдено: ${total}`;
  document.querySelector('#coverage').textContent=`В каталоге: ${data.catalog_documents} · С текстом: ${data.text_indexed_documents}`;
  statusLine.textContent='';
  if(!data.items.length)results.append(node('p','Ничего не найдено. Сократи фразу или сбрось фильтры.','empty'));
  else for(const book of data.items)results.append(card(book));
  document.querySelector('#pagination').hidden=total<=pageSize;document.querySelector('#prev').disabled=offset===0;document.querySelector('#next').disabled=offset+pageSize>=total;
  document.querySelector('#page').textContent=`${Math.floor(offset/pageSize)+1} / ${Math.max(1,Math.ceil(total/pageSize))}`;
 }catch(error){if(current===requestNumber){statusLine.textContent=error.name==='TimeoutError'?'Поиск занял слишком много времени. Попробуй ещё раз.':error.message==='Failed to fetch'?'Нет соединения с приложением. Убедись, что оно запущено.':error.message;document.querySelector('#result-title').textContent='Поиск не завершён';}}
 finally{if(current===requestNumber)results.setAttribute('aria-busy','false');}
}
form.addEventListener('submit',event=>{event.preventDefault();offset=0;activeQuery=new URLSearchParams();for(const [key,value] of new FormData(form))if(String(value).trim())activeQuery.set(key,String(value).trim());search();});
form.addEventListener('reset',()=>{offset=0;activeQuery=new URLSearchParams();search();});
document.querySelector('#prev').addEventListener('click',()=>{offset=Math.max(0,offset-pageSize);search();});
document.querySelector('#next').addEventListener('click',()=>{offset+=pageSize;search();});
search();

fetch('/api/v1/communities').then(response=>{if(!response.ok)throw new Error();return response.json();}).then(data=>{const select=document.querySelector('#community');for(const group of data.items){const option=node('option',group.display_name||group.domain);option.value=group.domain;select.append(option);}}).catch(()=>{const select=document.querySelector('#community');select.disabled=true;select.options[0].textContent='Список недоступен';});
